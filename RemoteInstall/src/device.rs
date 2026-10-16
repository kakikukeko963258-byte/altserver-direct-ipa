use anyhow::{Context, Result, ensure};
use idevice::{IdeviceService, RsdService, RemoteXpcClient,
    core_device_proxy::CoreDeviceProxy, lockdown::LockdownClient,
    remote_pairing::{RemotePairingClient, RpPairingFile, RpPairingSocket, connect_tls_psk_tunnel},
    rsd::RsdHandshake, tcp::{adapter::Adapter, handle::AdapterHandle},
    usbmuxd::{Connection, UsbmuxdAddr, UsbmuxdConnection}};
use std::{net::Ipv4Addr, time::Duration};
use crate::Config;

pub async fn pair(ip: Ipv4Addr, bind: Ipv4Addr, app_root: String, trusted_wifi: bool) -> Result<(Config, Vec<u8>)> {
    let mut mux = UsbmuxdConnection::default().await?;
    let devices: Vec<_> = mux.get_devices().await?.into_iter()
        .filter(|d| if trusted_wifi { matches!(&d.connection_type, Connection::Network(_)) } else { d.connection_type == Connection::Usb }).collect();
    // Never silently register another phone when multiple devices are attached.
    ensure!(devices.len() == 1, "Exactly one device is required on the selected trusted transport");
    let dev = &devices[0];
    let provider = dev.to_provider(UsbmuxdAddr::default(), "AltForgeRemote");
    let mut lockdown = LockdownClient::connect(&provider).await?;
    let name = lockdown.get_value(Some("DeviceName"), None).await?.as_string().context("name")?.to_owned();
    let version = lockdown.get_value(Some("ProductVersion"), None).await?.as_string().context("version")?.to_owned();
    let class = lockdown.get_value(Some("DeviceClass"), None).await?.as_string().context("class")?.to_owned();
    ensure!(class == "iPhone" || class == "iPad", "Unsupported device");
    let proxy = CoreDeviceProxy::connect(&provider).await?;
    let port = proxy.tunnel_info().server_rsd_port;
    let mut adapter = proxy.create_software_tunnel()?.to_async_handle();
    let handshake = RsdHandshake::new(adapter.connect(port).await?).await?;
    let port = handshake.services.get("com.apple.internal.dt.coredevice.untrusted.tunnelservice")
        .context("Pairing service unavailable; enable Developer Mode and unlock iPhone")?.port;
    let mut xpc = RemoteXpcClient::new(adapter.connect(port).await?).await?;
    xpc.do_handshake().await?;
    let _ = xpc.recv_root().await?;
    let mut pairing = RpPairingFile::generate("AltForgeRemote");
    let mut client = RemotePairingClient::new(xpc, "AltForgeRemote");
    // The existing trusted device tunnel authorizes pairing; a PIN requirement is not guessed.
    client.connect(&mut pairing, async || String::new()).await?;
    ensure!(client.paired_peer_device()?.remotepairing_udid == dev.udid, "Pairing identity mismatch");
    let mut random = [0u8; 32];
    openssl::rand::rand_bytes(&mut random)?;
    Ok((Config { ip, bind, udid: dev.udid.clone(), name, version,
        device_type: if class == "iPad" {4} else {2}, app_root,
        token: hex::encode(random) }, pairing.to_bytes()))
}

pub struct Session {
    pub handle: AdapterHandle,
    pub rsd: RsdHandshake,
    _control: RemotePairingClient<RpPairingSocket<tokio::net::TcpStream>>,
}
impl Session {
    pub async fn verify(&mut self, expected: &str) -> Result<()> {
        let mut lockdown = LockdownClient::connect_rsd(&mut self.handle, &mut self.rsd).await
            .inspect_err(|_| eprintln!("Diagnostic: device service connection failed"))?;
        let udid = lockdown.get_value(Some("UniqueDeviceID"), None).await
            .inspect_err(|_| eprintln!("Diagnostic: device identity request failed"))?;
        ensure!(udid.as_string() == Some(expected), "Device identity mismatch");
        Ok(())
    }
}

pub async fn open(config: &Config, bytes: &[u8]) -> Result<Session> {
    let mut pairing = RpPairingFile::from_bytes(bytes)?;
    let socket = tokio::net::TcpStream::connect((config.ip, 49152)).await?;
    let mut client = RemotePairingClient::new(RpPairingSocket::new(socket), "AltForgeRemote");
    client.attempt_pair_verify().await?;
    client.validate_pairing(&mut pairing).await?; // Never attempt new pairing over cellular.
    let port = client.create_tcp_listener().await?;
    let stream = tokio::net::TcpStream::connect((config.ip, port)).await?;
    // OpenSSL verifies TLS Finished/record authentication using the negotiated PSK.
    let tunnel = connect_tls_psk_tunnel(stream, client.encryption_key()).await?;
    let info = tunnel.info.clone();
    let mut adapter = Adapter::new(Box::new(tunnel.into_inner()), info.client_address.parse()?, info.server_address.parse()?);
    adapter.set_mss((info.mtu as usize).saturating_sub(60));
    let mut handle = adapter.to_async_handle();
    let rsd = RsdHandshake::new(handle.connect(info.server_rsd_port).await?).await?;
    let mut session = Session { handle, rsd, _control:client };
    session.verify(&config.udid).await?;
    Ok(session)
}

pub async fn checked_open(config: &Config, bytes: &[u8]) -> Result<Session> {
    tokio::time::timeout(Duration::from_secs(30), open(config, bytes)).await
        .context("VPN connection timed out")?
}

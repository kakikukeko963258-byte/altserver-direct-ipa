//! Same-machine IPC for the native signer. No signed-bundle path or raw-device
//! operation is exposed on the VPN HTTP listener. Authentication is mutual so a
//! fake pipe cannot collect the token or the signed application path.
use anyhow::{Result, ensure, Context};
use openssl::{pkey::PKey, sign::Signer, hash::MessageDigest};
use serde::{Serialize, Deserialize};
use subtle::ConstantTimeEq;
use tokio::io::{AsyncRead, AsyncWrite, AsyncReadExt, AsyncWriteExt};
use crate::Config;
use std::time::Duration;

#[derive(Serialize, Deserialize)]
#[serde(tag="command",rename_all="kebab-case",deny_unknown_fields)]
pub enum Request { Probe, Install {app: String, udid: String} }
#[derive(Serialize, Deserialize)]
struct Reply {success: bool}
fn mac(key:&[u8], phase:&[u8], client:&[u8], server:&[u8]) -> Result<Vec<u8>> {
    let key = PKey::hmac(key)?;
    let mut signer = Signer::new(MessageDigest::sha256(), &key)?;
    signer.update(b"AltForgeRemoteIPC-v1\0")?;
    signer.update(phase)?; signer.update(client)?; signer.update(server)?;
    Ok(signer.sign_to_vec()?)
}
async fn authenticate_server<S:AsyncRead+AsyncWrite+Unpin>(io:&mut S,key:&[u8]) -> Result<()> {
    let mut client = [0u8;32]; io.read_exact(&mut client).await?;
    let mut server = [0u8;32]; openssl::rand::rand_bytes(&mut server)?;
    io.write_all(&server).await?;
    io.write_all(&mac(key,b"server",&client,&server)?).await?;
    io.flush().await?;
    let mut proof = [0u8;32]; io.read_exact(&mut proof).await?;
    ensure!(bool::from(proof.as_slice().ct_eq(mac(key,b"client",&client,&server)?.as_slice())), "Client authentication failed");
    Ok(())
}
async fn authenticate_client<S:AsyncRead+AsyncWrite+Unpin>(io:&mut S,key:&[u8]) -> Result<()> {
    let mut client = [0u8;32]; openssl::rand::rand_bytes(&mut client)?;
    io.write_all(&client).await?; io.flush().await?;
    let mut server = [0u8;32]; io.read_exact(&mut server).await?;
    let mut proof = [0u8;32]; io.read_exact(&mut proof).await?;
    ensure!(bool::from(proof.as_slice().ct_eq(mac(key,b"server",&client,&server)?.as_slice())), "Server authentication failed");
    io.write_all(&mac(key,b"client",&client,&server)?).await?;
    io.flush().await?;
    Ok(())
}
async fn send<S:AsyncWrite+Unpin,T:Serialize>(io:&mut S,value:&T) -> Result<()> {
    let bytes = serde_json::to_vec(value)?;
    ensure!(!bytes.is_empty() && bytes.len() <= 16384, "IPC frame too large");
    io.write_u32(bytes.len() as u32).await?; io.write_all(&bytes).await?; io.flush().await?;
    Ok(())
}
async fn receive<S:AsyncRead+Unpin,T:serde::de::DeserializeOwned>(io:&mut S) -> Result<T> {
    let length = io.read_u32().await?;
    ensure!(length > 0 && length <= 16384, "Invalid IPC length");
    let mut bytes = vec![0u8;length as usize]; io.read_exact(&mut bytes).await?;
    Ok(serde_json::from_slice(&bytes)?)
}
fn name(config:&Config) -> String {
    let fingerprint = openssl::sha::sha256(config.token.as_bytes());
    format!(r"\\.\pipe\AltForgeRemoteV2-{}",hex::encode(&fingerprint[..16]))
}
#[cfg(windows)]
pub async fn call(config:&Config,request:Request) -> Result<()> {
    use tokio::net::windows::named_pipe::ClientOptions;
    let mut connected = None;
    for _ in 0..20 {
        match ClientOptions::new().open(name(config)) {
            Ok(pipe) => {connected = Some(pipe); break},
            Err(_) => tokio::time::sleep(Duration::from_millis(100)).await,
        }
    }
    let mut pipe = connected.context("Start the persistent bridge first").inspect_err(|_| eprintln!("Diagnostic: IPC open failed"))?;
    tokio::time::timeout(Duration::from_secs(5),authenticate_client(&mut pipe,config.token.as_bytes())).await
        .inspect_err(|_| eprintln!("Diagnostic: IPC authentication timed out"))?
        .inspect_err(|_| eprintln!("Diagnostic: IPC authentication failed"))?;
    send(&mut pipe,&request).await?;
    let reply:Reply = tokio::time::timeout(Duration::from_secs(930),receive(&mut pipe)).await??;
    // DisconnectNamedPipe discards unread buffered bytes. Acknowledge receipt
    // before the server disconnects; the verified reply determines the result.
    let _ = pipe.write_all(&[1]).await;
    let _ = pipe.flush().await;
    ensure!(reply.success,"Persistent bridge operation failed");
    Ok(())
}
#[cfg(windows)]
pub async fn serve(config:Config,bridge:std::sync::Arc<crate::bridge::Bridge>) -> Result<()> {
    use tokio::net::windows::named_pipe::ServerOptions;
    loop {
        // A disconnected Tokio handle may retain readiness from the previous
        // connection. Each client gets a fresh OS/IOCP handle and nonce stream.
        let mut pipe = ServerOptions::new().first_pipe_instance(true).max_instances(1).reject_remote_clients(true).create(name(&config))?;
        pipe.connect().await?;
        let authenticated = tokio::time::timeout(Duration::from_secs(5),authenticate_server(&mut pipe,config.token.as_bytes())).await;
        if matches!(authenticated,Ok(Ok(()))) {
            if let Ok(Ok(request)) = tokio::time::timeout(Duration::from_secs(5),receive::<_,Request>(&mut pipe)).await {
                let result = match request {
                    Request::Probe => bridge.verify().await,
                    Request::Install {app,udid} => bridge.install(std::path::Path::new(&app),&udid).await,
                };
                if result.is_err() { eprintln!("Diagnostic: broker operation failed"); }
                if send(&mut pipe,&Reply {success:result.is_ok()}).await.is_ok() {
                    let mut ack = [0u8;1];
                    let _ = tokio::time::timeout(Duration::from_secs(5),pipe.read_exact(&mut ack)).await;
                }
            }
        }
        let _ = pipe.disconnect();
    }
}
#[cfg(not(windows))]
pub async fn call(_: &Config,_:Request) -> Result<()> { anyhow::bail!("Windows named pipes required") }
#[cfg(not(windows))]
pub async fn serve(_:Config,_:std::sync::Arc<crate::bridge::Bridge>) -> Result<()> { anyhow::bail!("Windows named pipes required") }

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn authenticated_duplex_frames() {
        let (mut a,mut b) = tokio::io::duplex(4096);
        let server = tokio::spawn(async move {
            authenticate_server(&mut a,b"synthetic-key").await.unwrap();
            let request:Request = receive(&mut a).await.unwrap();
            assert!(matches!(request,Request::Probe));
            send(&mut a,&Reply {success:true}).await.unwrap();
        });
        authenticate_client(&mut b,b"synthetic-key").await.unwrap();
        send(&mut b,&Request::Probe).await.unwrap();
        assert!(receive::<_,Reply>(&mut b).await.unwrap().success);
        server.await.unwrap();
    }
    #[tokio::test]
    async fn wrong_server_key_rejected_before_request() {
        let (mut a,mut b) = tokio::io::duplex(4096);
        let server = tokio::spawn(async move {authenticate_server(&mut a,b"wrong-key").await.is_err()});
        assert!(authenticate_client(&mut b,b"right-key").await.is_err());
        drop(b); assert!(server.await.unwrap());
    }
    #[tokio::test]
    async fn reflected_server_proof_rejected() {
        let (mut a,mut b) = tokio::io::duplex(4096);
        let server = tokio::spawn(async move {authenticate_server(&mut a,b"key").await.is_err()});
        b.write_all(&[1u8;32]).await.unwrap();
        let mut nonce = [0u8;32]; let mut proof = [0u8;32];
        b.read_exact(&mut nonce).await.unwrap(); b.read_exact(&mut proof).await.unwrap();
        b.write_all(&proof).await.unwrap();
        assert!(server.await.unwrap());
    }
    #[tokio::test]
    async fn oversized_frame_rejected() {
        let (mut a,mut b) = tokio::io::duplex(16);
        a.write_u32(1000000).await.unwrap();
        assert!(receive::<_,Request>(&mut b).await.is_err());
    }
    #[cfg(windows)]
    #[tokio::test]
    async fn actual_windows_pipe_roundtrip() {
        use tokio::net::windows::named_pipe::{ServerOptions,ClientOptions};
        let mut random = [0u8;16]; openssl::rand::rand_bytes(&mut random).unwrap();
        let name = format!(r"\\.\pipe\AltForgeRemoteIPC-Test-{}",hex::encode(random));
        let first = ServerOptions::new().first_pipe_instance(true).max_instances(1).reject_remote_clients(true).create(&name).unwrap();
        let server_name = name.clone();
        let task = tokio::spawn(async move {
            let mut next = Some(first);
            for _ in 0..3 {
                let mut server = match next.take() {
                    Some(first) => first,
                    None => ServerOptions::new().first_pipe_instance(true).max_instances(1).reject_remote_clients(true).create(&server_name).unwrap(),
                };
                server.connect().await.unwrap();
                authenticate_server(&mut server,b"synthetic-test-key").await.unwrap();
                assert!(matches!(receive::<_,Request>(&mut server).await.unwrap(),Request::Probe));
                send(&mut server,&Reply {success:true}).await.unwrap();
                let mut ack = [0u8;1]; server.read_exact(&mut ack).await.unwrap();
                server.disconnect().unwrap();
            }
        });
        for _ in 0..3 {
            let mut client = tokio::time::timeout(Duration::from_secs(5),async {
                loop {
                    if let Ok(client) = ClientOptions::new().open(&name) { break client; }
                    tokio::time::sleep(Duration::from_millis(10)).await;
                }
            }).await.unwrap();
            authenticate_client(&mut client,b"synthetic-test-key").await.unwrap();
            send(&mut client,&Request::Probe).await.unwrap();
            assert!(receive::<_,Reply>(&mut client).await.unwrap().success);
            client.write_all(&[1]).await.unwrap();
            // Allow the server to receive the acknowledgement before closing.
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
        task.await.unwrap();
    }
}

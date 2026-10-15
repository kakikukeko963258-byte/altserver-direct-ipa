mod device;
mod portal;
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::{net::Ipv4Addr, path::PathBuf, time::Duration};

#[derive(Clone, Serialize, Deserialize)]
pub struct Config {
    ip: Ipv4Addr,
    bind: Ipv4Addr,
    udid: String,
    name: String,
    version: String,
    device_type: i32,
    app_root: String,
    #[serde(skip)]
    token: String,
}
pub fn directory() -> Result<PathBuf> {
    Ok(PathBuf::from(std::env::var_os("LOCALAPPDATA").context("LOCALAPPDATA missing")?)
        .join("AltServerDirectIPA").join("Remote"))
}
fn private(ip: Ipv4Addr) -> bool {
    (ip.is_private() || (ip.octets()[0] == 100 && (64..128).contains(&ip.octets()[1]))) && !ip.is_loopback() && ip.octets()[3] != 0 && ip.octets()[3] != 255
}
fn load() -> Result<(Config, Vec<u8>)> {
    let dir = directory()?;
    let mut config: Config = serde_json::from_slice(&std::fs::read(dir.join("config.json"))?)?;
    ensure!(private(config.ip) && private(config.bind) && config.ip != config.bind, "Invalid VPN endpoints");
    config.token = String::from_utf8(unprotect(&std::fs::read(dir.join("token.bin"))?)?)?;
    ensure!(config.token.len() == 64 && hex::decode(&config.token)?.len() == 32, "Invalid token");
    Ok((config, unprotect(&std::fs::read(dir.join("pairing.bin"))?)?))
}

// User-scoped DPAPI: neither pairing private keys nor the portal token are
// stored as plaintext. Do not enable tracing in idevice (it logs plist data).
#[cfg(windows)]
fn crypt(data: &[u8], protect: bool) -> Result<Vec<u8>> {
    use windows_sys::Win32::{Foundation::LocalFree, Security::Cryptography::*};
    let input = CRYPT_INTEGER_BLOB { cbData: data.len().try_into()?, pbData: data.as_ptr() as *mut u8 };
    let mut output = CRYPT_INTEGER_BLOB { cbData: 0, pbData: std::ptr::null_mut() };
    let success = unsafe {
        if protect {
            CryptProtectData(&input, std::ptr::null(), std::ptr::null(), std::ptr::null(), std::ptr::null(), CRYPTPROTECT_UI_FORBIDDEN, &mut output)
        } else {
            CryptUnprotectData(&input, std::ptr::null_mut(), std::ptr::null(), std::ptr::null(), std::ptr::null(), CRYPTPROTECT_UI_FORBIDDEN, &mut output)
        }
    };
    ensure!(success != 0, "Windows secret storage failed");
    let bytes = unsafe { std::slice::from_raw_parts(output.pbData, output.cbData as usize).to_vec() };
    unsafe {
        std::ptr::write_bytes(output.pbData, 0, output.cbData as usize);
        LocalFree(output.pbData as _);
    }
    Ok(bytes)
}
#[cfg(not(windows))]
fn crypt(_: &[u8], _: bool) -> Result<Vec<u8>> { anyhow::bail!("Windows DPAPI is required") }
fn unprotect(data: &[u8]) -> Result<Vec<u8>> { crypt(data, false) }

#[tokio::main]
async fn main() {
    // Error strings from device libraries may include account/device data.
    // Only fixed error stages reach the console and native installer.
    if run().await.is_err() { eprintln!("Remote operation failed. Check VPN, USB trust and local registration."); std::process::exit(1); }
}
async fn run() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("pair") if args.len() == 5 => {
            let ip = args[2].parse()?;
            let bind = args[3].parse()?;
            ensure!(private(ip) && private(bind) && ip != bind, "Use distinct private VPN IPv4 addresses");
            let root = std::fs::canonicalize(&args[4])?;
            ensure!(root.is_dir(), "IPA folder missing");
            let dir = directory()?;
            ensure!(dir.is_dir() && !dir.join("config.json").exists(), "Run setup first; existing registration will not be overwritten");
            println!("Keep exactly one iPhone connected over USB and unlocked. Accept Trust on iPhone if asked.");
            let (config, pairing) = tokio::time::timeout(Duration::from_secs(120), device::pair(ip, bind, root.to_string_lossy().into())).await??;
            std::fs::write(dir.join("pairing.bin"), crypt(&pairing, true)?)?;
            std::fs::write(dir.join("token.bin"), crypt(config.token.as_bytes(), true)?)?;
            std::fs::write(dir.join("config.json"), serde_json::to_vec_pretty(&config)?)?;
            println!("Registration saved locally. Next: connect both devices to your VPN, then run probe.");
        }
        Some("probe") if args.len() == 2 => {
            let (config, pairing) = load()?;
            device::checked_open(&config, &pairing).await?;
            println!("Registered iPhone verified over VPN.");
        }
        Some("install") if args.len() == 4 => {
            let (config, pairing) = load()?;
            ensure!(config.udid == args[3], "Signing target does not match registration");
            let path = PathBuf::from(&args[2]);
            // Called only by native signer. Accept the signed .app directory,
            // never arbitrary uploaded packages from the portal.
            ensure!(path.is_dir() && path.extension().is_some_and(|e| e == "app"), "Expected signed app bundle");
            let (mut handle, mut rsd) = device::checked_open(&config, &pairing).await?;
            tokio::time::timeout(Duration::from_secs(900),
                idevice::utils::installation::install_package_rsd(&mut handle, &mut rsd, &path, None)).await??;
            println!("InstallationProxy confirmed installation.");
        }
        Some("serve") if args.len() == 2 => {
            let (config, _) = load()?;
            portal::serve(config).await?;
        }
        Some("connection") if args.len() == 2 => {
            let (config, _) = load()?;
            // Explicit user-only export for entry in the iPhone controller.
            // Never printed by diagnostics, CI, or background serve.
            let content = serde_json::to_vec_pretty(&serde_json::json!({
                "url": format!("http://{}:49321", config.bind), "token": config.token
            }))?;
            std::fs::write(directory()?.join("iphone-connection.json"), content)?;
            println!("Connection settings saved in the private Remote folder. Delete iphone-connection.json after setup.");
        }
        _ => { anyhow::bail!("Usage: pair PHONE_VPN_IP PC_VPN_IP IPA_FOLDER | probe | serve | connection | install SIGNED_APP UDID"); }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn public_and_unspecified_addresses_rejected() {
        for ip in ["0.0.0.0", "127.0.0.1", "8.8.8.8", "224.0.0.1", "10.7.0.0", "10.7.0.255"] {
            assert!(!private(ip.parse().unwrap()));
        }
        assert!(private("10.77.0.2".parse().unwrap()));
    }
    #[cfg(windows)]
    #[test]
    fn dpapi_roundtrip_and_tamper() {
        let encrypted = crypt(b"synthetic pairing fixture", true).unwrap();
        assert_eq!(unprotect(&encrypted).unwrap(), b"synthetic pairing fixture");
        let mut corrupt = encrypted;
        let index = corrupt.len() / 2;
        corrupt[index] ^= 0x5a;
        assert!(unprotect(&corrupt).is_err());
    }
}

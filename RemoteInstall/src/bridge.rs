use crate::{Config, device};
use anyhow::{Context, Result, ensure};
use std::{path::Path, sync::{Arc, atomic::{AtomicBool, Ordering}}, time::Duration};
use tokio::sync::Mutex;

pub struct Bridge {
    config: Config,
    pairing: Vec<u8>,
    session: Mutex<Option<device::Session>>,
    connected: AtomicBool,
}
impl Bridge {
    pub fn new(config: Config, pairing: Vec<u8>) -> Arc<Self> {
        Arc::new(Self {config, pairing, session:Mutex::new(None), connected:AtomicBool::new(false)})
    }
    pub fn connected(&self) -> bool { self.connected.load(Ordering::Acquire) }
    pub fn start(self: &Arc<Self>) {
        let bridge = self.clone();
        tokio::spawn(async move {
            loop {
                {
                    let mut session = bridge.session.lock().await;
                    if session.is_none() {
                        // A cold cellular listener may be closed. Retry only
                        // acquisition; never silently repeat an installation.
                        if let Ok(opened) = device::checked_open(&bridge.config, &bridge.pairing).await {
                            *session = Some(opened);
                            bridge.connected.store(true, Ordering::Release);
                        }
                    } else if let Some(active) = session.as_mut() {
                        let live = tokio::time::timeout(Duration::from_secs(30), active.verify(&bridge.config.udid)).await;
                        if !matches!(live, Ok(Ok(()))) {
                            *session = None;
                            bridge.connected.store(false, Ordering::Release);
                        }
                    }
                }
                tokio::time::sleep(Duration::from_secs(15)).await;
            }
        });
    }
    pub async fn verify(&self) -> Result<()> {
        let mut guard = self.session.lock().await;
        let active = guard.as_mut().context("Connect iPhone to Wi-Fi once to acquire the bridge")?;
        let result = tokio::time::timeout(Duration::from_secs(30), active.verify(&self.config.udid)).await
            .context("Existing tunnel timed out").and_then(|r| r);
        if result.is_err() { *guard = None; self.connected.store(false, Ordering::Release); }
        result
    }
    pub async fn install(&self, path: &Path, udid: &str) -> Result<()> {
        ensure!(udid == self.config.udid, "Target changed");
        ensure!(path.is_dir() && path.extension().is_some_and(|e| e == "app"), "Expected signed app");
        let mut guard = self.session.lock().await;
        let active = guard.as_mut().context("Bridge is not connected")?;
        let outcome = tokio::time::timeout(Duration::from_secs(900), async {
            active.verify(udid).await?;
            idevice::utils::installation::install_package_rsd(&mut active.handle, &mut active.rsd, path, None).await?;
            Ok::<(),anyhow::Error>(())
        }).await;
        let timed_out = outcome.is_err();
        let result = outcome.context("Install timed out").and_then(|r| r);
        if result.is_err() {
            // App/profile rejection must not destroy a healthy outer tunnel:
            // that would strand the user on cellular until Wi-Fi is available.
            let live = !timed_out && matches!(tokio::time::timeout(Duration::from_secs(20),active.verify(udid)).await,Ok(Ok(())));
            if !live { *guard = None; self.connected.store(false, Ordering::Release); }
        }
        result
    }
}

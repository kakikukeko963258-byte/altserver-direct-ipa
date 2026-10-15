use crate::Config;
use anyhow::{Result, ensure};
use axum::{Router, Json, extract::{State, DefaultBodyLimit, Request}, http::{StatusCode, HeaderMap, header},
    middleware::{self, Next}, response::Response, routing::{get, post}};
use serde::{Serialize, Deserialize};
use std::{path::{Path, PathBuf}, sync::Arc, time::Duration};
use subtle::ConstantTimeEq;
use tokio::sync::Mutex;

#[derive(Clone)]
struct App { config: Config, root: PathBuf, native: PathBuf, job: Arc<Mutex<Job>> }
#[derive(Clone, Serialize, Default)]
struct Job { id: u64, state: String }
#[derive(Serialize)]
struct Entry { id: String, name: String }
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Install { id: String }

fn allowed_file(root: &Path, id: &str) -> Result<PathBuf> {
    // An opaque filename is the only remote input. One directory, no uploads,
    // subdirectories, UNC paths, alternate streams, junctions or symlinks.
    ensure!(!id.is_empty() && id.len() <= 240 && !id.contains(['/', '\\', ':']) && id != "." && id != "..", "Invalid app id");
    let path = root.join(id);
    ensure!(path.extension().is_some_and(|e| e.eq_ignore_ascii_case("ipa")), "Not IPA");
    let meta = std::fs::symlink_metadata(&path)?;
    ensure!(meta.is_file() && !meta.file_type().is_symlink() && meta.len() > 0 && meta.len() <= 4 * 1024 * 1024 * 1024, "Invalid file");
    let path = std::fs::canonicalize(path)?;
    ensure!(path.parent() == Some(root), "Outside allowlist");
    Ok(path)
}

async fn authorize(State(app): State<App>, req: Request, next: Next) -> Result<Response, StatusCode> {
    // Browsers are not clients: reject cross-origin requests even with a token.
    if req.headers().contains_key(header::ORIGIN) { return Err(StatusCode::FORBIDDEN); }
    let expected = format!("Bearer {}", app.config.token);
    let actual = req.headers().get(header::AUTHORIZATION).and_then(|h| h.to_str().ok()).unwrap_or("");
    if !bool::from(expected.as_bytes().ct_eq(actual.as_bytes())) { return Err(StatusCode::UNAUTHORIZED); }
    let mut response = next.run(req).await;
    response.headers_mut().insert(header::CACHE_CONTROL, "no-store".parse().unwrap());
    Ok(response)
}
async fn apps(State(app): State<App>) -> Result<Json<Vec<Entry>>, StatusCode> {
    let mut list = Vec::new();
    for entry in std::fs::read_dir(&app.root).map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?.take(1000).flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if allowed_file(&app.root, &name).is_ok() { list.push(Entry { id: name.clone(), name }); }
    }
    list.sort_by(|a,b| a.name.cmp(&b.name));
    Ok(Json(list))
}
async fn status(State(app): State<App>) -> Json<Job> { Json(app.job.lock().await.clone()) }
async fn install(State(app): State<App>, headers: HeaderMap, Json(request): Json<Install>) -> Result<Json<Job>, StatusCode> {
    let path = allowed_file(&app.root, &request.id).map_err(|_| StatusCode::BAD_REQUEST)?;
    // Idempotency prevents a retry/double tap from installing the same job twice.
    let key = headers.get("X-Request-ID").and_then(|v| v.to_str().ok()).ok_or(StatusCode::BAD_REQUEST)?;
    let key: u64 = key.parse().map_err(|_| StatusCode::BAD_REQUEST)?;
    if key == 0 { return Err(StatusCode::BAD_REQUEST); }
    let mut job = app.job.lock().await;
    if job.id == key { return Ok(Json(job.clone())); }
    if matches!(job.state.as_str(), "queued" | "installing" | "awaiting-apple-id" | "unknown") { return Err(StatusCode::CONFLICT); }
    *job = Job { id: key, state: "queued".into() };
    let accepted = job.clone();
    drop(job);
    tokio::spawn(async move {
        let outcome = run_install(&app, path).await;
        app.job.lock().await.state = outcome.unwrap_or_else(|_| "failed".into());
    });
    Ok(Json(accepted))
}
async fn run_install(app: &App, path: PathBuf) -> Result<String> {
    let state_file = crate::directory()?.join("state.txt");
    // canonicalize() uses the Win32 verbatim prefix; the native local-file
    // validator intentionally accepts conventional absolute drive paths only.
    let native_path = path.to_string_lossy();
    let native_path = native_path.strip_prefix(r"\\?\").unwrap_or(&native_path);
    // Launcher exit 0 means the native process accepted and reset the dedicated
    // remote state; a rejected request must not reuse an earlier success.
    let exit = tokio::process::Command::new(&app.native).arg("--install-remote").arg(native_path)
        .stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null()).status().await?;
    ensure!(exit.success(), "Native server rejected request");
    for _ in 0..1800 {
        let stage = tokio::fs::read_to_string(&state_file).await.unwrap_or_default();
        match stage.trim() {
            "succeeded" => return Ok("succeeded".into()),
            "failed" | "cancelled" | "login-dialog-failed" => return Ok("failed".into()),
            "awaiting-apple-id" => app.job.lock().await.state = "awaiting-apple-id".into(),
            _ => app.job.lock().await.state = "installing".into(),
        }
        tokio::time::sleep(Duration::from_secs(1)).await;
    }
    // Do not allow a second job after a timeout; native signing may still run.
    Ok("unknown".into())
}
fn router(app: App) -> Router {
    Router::new().route("/api/apps", get(apps)).route("/api/status", get(status))
        .route("/api/install", post(install)).layer(DefaultBodyLimit::max(2048))
        .layer(middleware::from_fn_with_state(app.clone(), authorize)).with_state(app)
}
pub async fn serve(config: Config) -> Result<()> {
    let native = std::env::current_exe()?.parent().unwrap().join("AltServer.exe");
    ensure!(native.is_file(), "Put worker next to modified AltServer.exe");
    let root = std::fs::canonicalize(&config.app_root)?;
    let listener = tokio::net::TcpListener::bind((config.bind, 49321)).await?;
    let app = App { config, root, native, job: Arc::new(Mutex::new(Job::default())) };
    println!("Remote controller is listening on the configured private VPN interface only.");
    axum::serve(listener, router(app)).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::body::Body;
    use tower::ServiceExt;
    fn fixture(root: &Path) -> App {
        App { config: Config {ip:"10.77.0.2".parse().unwrap(),bind:"10.77.0.1".parse().unwrap(), udid:"synthetic".into(),name:"fixture".into(),version:"26.6.1".into(), device_type:2, app_root:root.to_string_lossy().into(),token:"a".repeat(64)}, root: std::fs::canonicalize(root).unwrap(),native:PathBuf::from("missing"),job:Arc::new(Mutex::new(Job::default())) }
    }
    #[test]
    fn traversal_and_wrong_extension_rejected() {
        let temp = tempfile::tempdir().unwrap();
        let root = std::fs::canonicalize(temp.path()).unwrap();
        std::fs::write(root.join("ok.ipa"), b"fixture").unwrap();
        assert!(allowed_file(&root,"ok.ipa").is_ok());
        for name in ["../ok.ipa", "..\\ok.ipa", "C:\\ok.ipa", "ok.ipa:stream", "ok.txt", ""] { assert!(allowed_file(&root,name).is_err()); }
    }
    #[tokio::test]
    async fn missing_wrong_token_and_browser_rejected() {
        let temp = tempfile::tempdir().unwrap();
        let app = fixture(temp.path());
        for token in ["", "Bearer incorrect"] {
            let res = router(app.clone()).oneshot(Request::builder().uri("/api/apps").header("Authorization",token).body(Body::empty()).unwrap()).await.unwrap();
            assert_eq!(res.status(),StatusCode::UNAUTHORIZED);
        }
        let res = router(app.clone()).oneshot(Request::builder().uri("/api/apps").header("Authorization",format!("Bearer {}",app.config.token)).header("Origin","https://example.invalid").body(Body::empty()).unwrap()).await.unwrap();
        assert_eq!(res.status(),StatusCode::FORBIDDEN);
        let res = router(app.clone()).oneshot(Request::builder().uri("/api/apps").header("Authorization",format!("Bearer {}",app.config.token)).body(Body::empty()).unwrap()).await.unwrap();
        assert_eq!(res.status(),StatusCode::OK);
        assert_eq!(res.headers()["cache-control"],"no-store");
    }
    #[tokio::test]
    async fn duplicate_is_idempotent_and_busy_conflicts() {
        let temp = tempfile::tempdir().unwrap();
        let app = fixture(temp.path());
        std::fs::write(app.root.join("ok.ipa"), b"fixture").unwrap();
        *app.job.lock().await = Job { id:42,state:"installing".into() };
        for (key,expected) in [(42,StatusCode::OK),(43,StatusCode::CONFLICT)] {
            let res = router(app.clone()).oneshot(Request::builder().method("POST").uri("/api/install")
                .header("Authorization",format!("Bearer {}",app.config.token)).header("Content-Type","application/json")
                .header("X-Request-ID",key.to_string()).body(Body::from(r#"{"id":"ok.ipa"}"#)).unwrap()).await.unwrap();
            assert_eq!(res.status(),expected);
        }
    }
}

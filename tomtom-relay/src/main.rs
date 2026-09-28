use std::env;
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufRead, BufReader, Read, Write};
use std::net::{SocketAddr, TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const DEFAULT_BIND: &str = "192.168.101.114:18744";
const DEFAULT_STORAGE: &str = "/home/sepisotoni/TomTomStorage";
const DEFAULT_WEATHER_URL: &str = "https://tomtom-opentom-research.onrender.com/v1/weather";
const MAX_HEADER_BYTES: usize = 16 * 1024;
const MAX_WEATHER_BYTES: usize = 1024;
const MAX_ASSET_BYTES: u64 = 32 * 1024 * 1024;
const MAX_RESPONSE_BYTES: u64 = 8192;
const MAX_CLIENTS: usize = 8;

static TEMP_COUNTER: AtomicU64 = AtomicU64::new(0);
static ACTIVE_CLIENTS: AtomicUsize = AtomicUsize::new(0);

#[derive(Clone)]
struct Config {
    bind: SocketAddr,
    storage: PathBuf,
    service_token: Option<String>,
    weather_token: Option<String>,
    weather_url: String,
}

struct Request {
    method: String,
    path: String,
    headers: Vec<(String, String)>,
    reader: BufReader<TcpStream>,
}

struct Response {
    status: u16,
    reason: &'static str,
    content_type: &'static str,
    body: Vec<u8>,
    file: Option<File>,
    content_length: u64,
    headers: Vec<(&'static str, String)>,
}

fn response(status: u16, reason: &'static str, body: impl Into<Vec<u8>>) -> Response {
    let body = body.into();
    Response {
        status,
        reason,
        content_type: "application/json; charset=utf-8",
        content_length: body.len() as u64,
        body,
        file: None,
        headers: Vec::new(),
    }
}

fn json_error(status: u16, code: &'static str) -> Response {
    response(
        status,
        status_reason(status),
        format!("{{\"error\":\"{code}\"}}").into_bytes(),
    )
}

fn status_reason(status: u16) -> &'static str {
    match status {
        200 => "OK",
        201 => "Created",
        204 => "No Content",
        400 => "Bad Request",
        401 => "Unauthorized",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        409 => "Conflict",
        413 => "Payload Too Large",
        415 => "Unsupported Media Type",
        500 => "Internal Server Error",
        503 => "Service Unavailable",
        507 => "Insufficient Storage",
        _ => "Error",
    }
}

fn load_config() -> Result<Config, String> {
    let bind_text = env::var("TOMTOM_RELAY_BIND").unwrap_or_else(|_| DEFAULT_BIND.into());
    let bind = bind_text
        .parse::<SocketAddr>()
        .map_err(|_| "TOMTOM_RELAY_BIND must be an IPv4 or IPv6 socket address".to_string())?;
    let storage =
        PathBuf::from(env::var("TOMTOM_STORAGE_DIR").unwrap_or_else(|_| DEFAULT_STORAGE.into()));
    let service_token = env::var("TOMTOM_SERVICE_TOKEN")
        .ok()
        .filter(|value| !value.is_empty());
    if bind.ip().to_string() != "192.168.101.114" && service_token.is_none() {
        return Err(
            "TOMTOM_SERVICE_TOKEN is required unless bound to the direct USB address".into(),
        );
    }
    if let Some(token) = &service_token {
        if !token.bytes().all(is_safe_token_byte) {
            return Err("TOMTOM_SERVICE_TOKEN contains unsupported characters".into());
        }
    }

    let weather_token = env::var("WEATHER_SERVICE_TOKEN")
        .ok()
        .filter(|value| !value.is_empty());
    if let Some(token) = &weather_token {
        if !token.bytes().all(is_safe_token_byte) {
            return Err("WEATHER_SERVICE_TOKEN contains unsupported characters".into());
        }
    }
    let weather_url = env::var("TOMTOM_WEATHER_URL").unwrap_or_else(|_| DEFAULT_WEATHER_URL.into());
    if !weather_url.starts_with("https://")
        || weather_url.bytes().any(|byte| byte.is_ascii_whitespace())
        || weather_url.contains('"')
        || weather_url.contains('\\')
    {
        return Err("TOMTOM_WEATHER_URL must be a safe HTTPS URL".into());
    }

    fs::create_dir_all(&storage)
        .map_err(|_| format!("cannot create storage directory {}", storage.display()))?;
    Ok(Config {
        bind,
        storage,
        service_token,
        weather_token,
        weather_url,
    })
}

fn is_safe_token_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || b"-._~+/=".contains(&byte)
}

fn main() {
    let config = match load_config() {
        Ok(config) => Arc::new(config),
        Err(error) => {
            eprintln!("tomtom-relay: {error}");
            std::process::exit(2);
        }
    };
    let listener = match TcpListener::bind(config.bind) {
        Ok(listener) => listener,
        Err(error) => {
            eprintln!("tomtom-relay: cannot listen on {}: {error}", config.bind);
            std::process::exit(1);
        }
    };
    eprintln!(
        "TomTom relay listening on {}; asset storage: {}",
        config.bind,
        config.storage.display()
    );
    for incoming in listener.incoming() {
        match incoming {
            Ok(stream) => {
                if ACTIVE_CLIENTS.fetch_add(1, Ordering::AcqRel) >= MAX_CLIENTS {
                    ACTIVE_CLIENTS.fetch_sub(1, Ordering::AcqRel);
                    let mut stream = stream;
                    let _ = send_response(&mut stream, json_error(503, "too_many_connections"));
                    continue;
                }
                let config = Arc::clone(&config);
                thread::spawn(move || {
                    let _active = ActiveClient;
                    if let Err(error) = handle_connection(stream, &config) {
                        eprintln!("tomtom-relay: request failed: {error}");
                    }
                });
            }
            Err(error) => eprintln!("tomtom-relay: accept failed: {error}"),
        }
    }
}

struct ActiveClient;

impl Drop for ActiveClient {
    fn drop(&mut self) {
        ACTIVE_CLIENTS.fetch_sub(1, Ordering::AcqRel);
    }
}

fn handle_connection(stream: TcpStream, config: &Config) -> io::Result<()> {
    stream.set_read_timeout(Some(Duration::from_secs(30)))?;
    stream.set_write_timeout(Some(Duration::from_secs(15)))?;
    let mut writer = stream.try_clone()?;
    let request = match read_request(stream) {
        Ok(request) => request,
        Err(reply) => {
            send_response(&mut writer, reply)?;
            return Ok(());
        }
    };
    let response = route_request(request, config);
    send_response(&mut writer, response)
}

fn read_request(stream: TcpStream) -> Result<Request, Response> {
    let mut reader = BufReader::new(stream);
    let mut line = String::new();
    if reader.read_line(&mut line).unwrap_or(0) == 0 || line.len() > 2048 {
        return Err(json_error(400, "invalid_request_line"));
    }
    let mut parts = line.split_whitespace();
    let method = match parts.next() {
        Some(value) if value.bytes().all(|byte| byte.is_ascii_uppercase()) => value.to_string(),
        _ => return Err(json_error(400, "invalid_method")),
    };
    let path = match (parts.next(), parts.next(), parts.next()) {
        (Some(path), Some("HTTP/1.1"), None) if path.starts_with('/') => path.to_string(),
        _ => return Err(json_error(400, "invalid_request_line")),
    };
    let mut headers = Vec::new();
    let mut total = line.len();
    loop {
        line.clear();
        let count = match reader.read_line(&mut line) {
            Ok(count) => count,
            Err(_) => return Err(json_error(400, "incomplete_headers")),
        };
        if count == 0 {
            return Err(json_error(400, "incomplete_headers"));
        }
        total += count;
        if total > MAX_HEADER_BYTES {
            return Err(json_error(413, "headers_too_large"));
        }
        if line == "\r\n" || line == "\n" {
            break;
        }
        let Some((name, value)) = line.split_once(':') else {
            return Err(json_error(400, "invalid_header"));
        };
        let name = name.trim().to_ascii_lowercase();
        let value = value.trim().to_string();
        if name.is_empty()
            || name
                .bytes()
                .any(|byte| !byte.is_ascii_alphanumeric() && byte != b'-')
        {
            return Err(json_error(400, "invalid_header"));
        }
        if headers
            .iter()
            .any(|(existing, _): &(String, String)| existing == &name)
        {
            return Err(json_error(400, "duplicate_header"));
        }
        headers.push((name, value));
    }
    if headers.iter().any(|(name, value)| {
        (name == "transfer-encoding" && !value.eq_ignore_ascii_case("identity")) || name == "expect"
    }) {
        return Err(json_error(400, "unsupported_transfer_encoding"));
    }
    Ok(Request {
        method,
        path,
        headers,
        reader,
    })
}

fn route_request(mut request: Request, config: &Config) -> Response {
    if let Some(token) = &config.service_token {
        let Some((_, supplied)) = request
            .headers
            .iter()
            .find(|(name, _)| name == "authorization")
        else {
            return json_error(401, "unauthorized");
        };
        if !constant_time_eq(supplied.as_bytes(), format!("Bearer {token}").as_bytes()) {
            return json_error(401, "unauthorized");
        }
    }

    if request.path == "/healthz" && request.method == "GET" {
        let weather_ready = config.weather_token.is_some();
        return response(
            200,
            "OK",
            format!(
                "{{\"status\":\"ok\",\"weather_configured\":{weather_ready},\"storage\":\"ready\"}}"
            )
            .into_bytes(),
        );
    }

    if request.path == "/v1/weather" {
        if request.method != "POST" {
            return json_error(405, "method_not_allowed");
        }
        if content_type(&request) != Some("application/json") {
            return json_error(415, "content_type_must_be_json");
        }
        let Some(length) = content_length(&request) else {
            return json_error(400, "content_length_required");
        };
        if length == 0 || length > MAX_WEATHER_BYTES {
            return json_error(413, "weather_request_size_invalid");
        }
        let mut body = vec![0; length];
        if request.reader.read_exact(&mut body).is_err() {
            return json_error(400, "incomplete_request_body");
        }
        return proxy_weather(&body, config);
    }

    if request.path == "/v1/files" {
        if request.method != "GET" {
            return json_error(405, "method_not_allowed");
        }
        return list_files(&config.storage);
    }

    if let Some(encoded_name) = request.path.strip_prefix("/v1/files/") {
        if !valid_filename(encoded_name) {
            return json_error(400, "invalid_filename");
        }
        let path = config.storage.join(encoded_name);
        match request.method.as_str() {
            "GET" => get_file(&path),
            "PUT" => put_file(&mut request, &path, &config.storage),
            "DELETE" => delete_file(&path),
            _ => json_error(405, "method_not_allowed"),
        }
    } else {
        json_error(404, "not_found")
    }
}

fn content_length(request: &Request) -> Option<usize> {
    request
        .headers
        .iter()
        .find(|(name, _)| name == "content-length")
        .and_then(|(_, value)| value.parse().ok())
}

fn content_type(request: &Request) -> Option<&str> {
    request
        .headers
        .iter()
        .find(|(name, _)| name == "content-type")
        .map(|(_, value)| value.split(';').next().unwrap_or("").trim())
}

fn constant_time_eq(left: &[u8], right: &[u8]) -> bool {
    let mut difference = left.len() ^ right.len();
    let max_len = left.len().max(right.len());
    for index in 0..max_len {
        difference |= usize::from(
            left.get(index).copied().unwrap_or(0) ^ right.get(index).copied().unwrap_or(0),
        );
    }
    difference == 0
}

fn valid_filename(name: &str) -> bool {
    if name.is_empty()
        || name.len() > 128
        || name.starts_with('.')
        || name.contains("..")
        || name.contains('/')
        || name.contains('\\')
    {
        return false;
    }
    let mut bytes = name.bytes();
    matches!(bytes.next(), Some(first) if first.is_ascii_alphanumeric())
        && bytes.all(|byte| byte.is_ascii_alphanumeric() || b"._-".contains(&byte))
}

fn list_files(storage: &Path) -> Response {
    let entries = match fs::read_dir(storage) {
        Ok(entries) => entries,
        Err(_) => return json_error(500, "storage_unavailable"),
    };
    let mut files = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        let Ok(metadata) = fs::symlink_metadata(&path) else {
            continue;
        };
        let Some(name) = entry.file_name().to_str().map(str::to_string) else {
            continue;
        };
        if metadata.is_file() && valid_filename(&name) {
            files.push((name, metadata.len()));
        }
    }
    files.sort_by(|a, b| a.0.cmp(&b.0));
    let mut body = String::from("{\"files\":[");
    for (index, (name, size)) in files.iter().enumerate() {
        if index != 0 {
            body.push(',');
        }
        body.push_str(&format!("{{\"name\":\"{name}\",\"size\":{size}}}"));
    }
    body.push_str("]}");
    response(200, "OK", body.into_bytes())
}

fn get_file(path: &Path) -> Response {
    let metadata = match fs::symlink_metadata(path) {
        Ok(metadata) if metadata.is_file() => metadata,
        Ok(_) => return json_error(404, "file_not_found"),
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            return json_error(404, "file_not_found")
        }
        Err(_) => return json_error(500, "storage_unavailable"),
    };
    if metadata.len() > MAX_ASSET_BYTES {
        return json_error(413, "file_exceeds_size_limit");
    }
    let file = match File::open(path) {
        Ok(file) => file,
        Err(_) => return json_error(404, "file_not_found"),
    };
    let mut reply = response(200, "OK", Vec::new());
    reply.content_type = "application/octet-stream";
    reply.content_length = metadata.len();
    reply.file = Some(file);
    reply
        .headers
        .push(("Cache-Control", "no-cache".to_string()));
    reply
}

fn put_file(request: &mut Request, path: &Path, storage: &Path) -> Response {
    let Some(length) = content_length(request).map(|value| value as u64) else {
        return json_error(400, "content_length_required");
    };
    if length == 0 || length > MAX_ASSET_BYTES {
        return json_error(413, "file_size_invalid");
    }
    if let Ok(metadata) = fs::symlink_metadata(path) {
        if !metadata.is_file() {
            return json_error(409, "target_is_not_regular_file");
        }
    }
    let temporary = storage.join(format!(
        ".upload-{}-{}",
        std::process::id(),
        TEMP_COUNTER.fetch_add(1, Ordering::Relaxed)
    ));
    let mut output = match OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temporary)
    {
        Ok(file) => file,
        Err(_) => return json_error(500, "storage_write_failed"),
    };
    let mut remaining = length;
    let mut buffer = [0u8; 16 * 1024];
    while remaining > 0 {
        let count = usize::min(buffer.len(), remaining as usize);
        match request.reader.read_exact(&mut buffer[..count]) {
            Ok(()) => {
                if output.write_all(&buffer[..count]).is_err() {
                    let _ = fs::remove_file(&temporary);
                    return json_error(500, "storage_write_failed");
                }
                remaining -= count as u64;
            }
            Err(_) => {
                let _ = fs::remove_file(&temporary);
                return json_error(400, "incomplete_request_body");
            }
        }
    }
    if output.sync_all().is_err() {
        let _ = fs::remove_file(&temporary);
        return json_error(500, "storage_write_failed");
    }
    drop(output);
    if fs::rename(&temporary, path).is_err() {
        let _ = fs::remove_file(&temporary);
        return json_error(500, "storage_write_failed");
    }
    response(201, "Created", b"{\"status\":\"stored\"}".to_vec())
}

fn delete_file(path: &Path) -> Response {
    match fs::symlink_metadata(path) {
        Ok(metadata) if metadata.is_file() => {}
        Ok(_) => return json_error(404, "file_not_found"),
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            return json_error(404, "file_not_found")
        }
        Err(_) => return json_error(500, "storage_unavailable"),
    }
    match fs::remove_file(path) {
        Ok(()) => response(200, "OK", b"{\"status\":\"deleted\"}".to_vec()),
        Err(_) => json_error(500, "storage_delete_failed"),
    }
}

fn proxy_weather(body: &[u8], config: &Config) -> Response {
    let Some(token) = &config.weather_token else {
        return json_error(503, "weather_proxy_not_configured");
    };
    let request = match parse_weather_request(body) {
        Ok(request) => request,
        Err(code) => return json_error(400, code),
    };
    let payload = format!(
        "{{\"latitude\":{:.2},\"longitude\":{:.2},\"location_sharing_enabled\":true,\"language_code\":\"{}\"}}",
        request.latitude, request.longitude, request.language
    );
    match call_weather_provider(&payload, token, &config.weather_url) {
        Ok(body) => {
            let mut result = response(200, "OK", body);
            result.headers.push(("Cache-Control", "no-store".into()));
            result
        }
        Err(code) => json_error(502, code),
    }
}

#[derive(Debug)]
struct WeatherRequest {
    latitude: f64,
    longitude: f64,
    language: String,
}

fn parse_weather_request(input: &[u8]) -> Result<WeatherRequest, &'static str> {
    let mut parser = JsonParser { input, cursor: 0 };
    parser.skip_whitespace();
    parser.expect(b'{')?;
    let mut latitude = None;
    let mut longitude = None;
    let mut consent = None;
    let mut language = None;
    parser.skip_whitespace();
    if parser.peek() != Some(b'}') {
        loop {
            let key = parser.string()?;
            parser.skip_whitespace();
            parser.expect(b':')?;
            parser.skip_whitespace();
            match key.as_str() {
                "latitude" if latitude.is_none() => latitude = Some(parser.number()?),
                "longitude" if longitude.is_none() => longitude = Some(parser.number()?),
                "location_sharing_enabled" if consent.is_none() => {
                    consent = Some(parser.boolean()?)
                }
                "language_code" if language.is_none() => language = Some(parser.string()?),
                _ => return Err("invalid_weather_request"),
            }
            parser.skip_whitespace();
            match parser.peek() {
                Some(b',') => {
                    parser.cursor += 1;
                    parser.skip_whitespace();
                }
                Some(b'}') => break,
                _ => return Err("invalid_weather_request"),
            }
        }
    }
    parser.expect(b'}')?;
    parser.skip_whitespace();
    if parser.cursor != input.len() {
        return Err("invalid_weather_request");
    }
    let latitude = latitude.ok_or("invalid_weather_request")?;
    let longitude = longitude.ok_or("invalid_weather_request")?;
    if consent != Some(true) {
        return Err("location_sharing_consent_required");
    }
    if !(-90.0..=90.0).contains(&latitude) || !(-180.0..=180.0).contains(&longitude) {
        return Err("invalid_coordinates");
    }
    let language = language.unwrap_or_else(|| "en".into());
    if language.is_empty()
        || language.len() > 35
        || !language
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
        || !language.as_bytes()[0].is_ascii_alphabetic()
        || language.contains("--")
    {
        return Err("invalid_language_code");
    }
    Ok(WeatherRequest {
        latitude,
        longitude,
        language,
    })
}

struct JsonParser<'a> {
    input: &'a [u8],
    cursor: usize,
}

impl JsonParser<'_> {
    fn peek(&self) -> Option<u8> {
        self.input.get(self.cursor).copied()
    }

    fn skip_whitespace(&mut self) {
        while self.peek().is_some_and(|byte| b" \t\r\n".contains(&byte)) {
            self.cursor += 1;
        }
    }

    fn expect(&mut self, expected: u8) -> Result<(), &'static str> {
        if self.peek() == Some(expected) {
            self.cursor += 1;
            Ok(())
        } else {
            Err("invalid_weather_request")
        }
    }

    fn string(&mut self) -> Result<String, &'static str> {
        self.expect(b'"')?;
        let start = self.cursor;
        while let Some(byte) = self.peek() {
            match byte {
                b'"' => {
                    let value = std::str::from_utf8(&self.input[start..self.cursor])
                        .map_err(|_| "invalid_weather_request")?;
                    if value.bytes().any(|byte| byte < 0x20 || byte == b'\\') {
                        return Err("invalid_weather_request");
                    }
                    self.cursor += 1;
                    return Ok(value.to_string());
                }
                b'\\' | 0..=0x1f => return Err("invalid_weather_request"),
                _ => self.cursor += 1,
            }
        }
        Err("invalid_weather_request")
    }

    fn number(&mut self) -> Result<f64, &'static str> {
        let start = self.cursor;
        while self
            .peek()
            .is_some_and(|byte| byte.is_ascii_digit() || b"-+.eE".contains(&byte))
        {
            self.cursor += 1;
        }
        if start == self.cursor {
            return Err("invalid_weather_request");
        }
        let text = std::str::from_utf8(&self.input[start..self.cursor])
            .map_err(|_| "invalid_weather_request")?;
        let value = text.parse::<f64>().map_err(|_| "invalid_weather_request")?;
        if !value.is_finite() {
            return Err("invalid_coordinates");
        }
        Ok(value)
    }

    fn boolean(&mut self) -> Result<bool, &'static str> {
        if self.input[self.cursor..].starts_with(b"true") {
            self.cursor += 4;
            Ok(true)
        } else if self.input[self.cursor..].starts_with(b"false") {
            self.cursor += 5;
            Ok(false)
        } else {
            Err("invalid_weather_request")
        }
    }
}

fn call_weather_provider(payload: &str, token: &str, url: &str) -> Result<Vec<u8>, &'static str> {
    let runtime = env::temp_dir().join(format!("tomtom-relay-{}", std::process::id()));
    fs::create_dir_all(&runtime).map_err(|_| "weather_proxy_failed")?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&runtime, fs::Permissions::from_mode(0o700))
            .map_err(|_| "weather_proxy_failed")?;
    }
    let id = TEMP_COUNTER.fetch_add(1, Ordering::Relaxed);
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| "weather_proxy_failed")?
        .as_nanos();
    let config_path = runtime.join(format!("curl-{now}-{id}.conf"));
    let body_path = runtime.join(format!("body-{now}-{id}.json"));
    let mut body_file = private_create(&body_path).map_err(|_| "weather_proxy_failed")?;
    if body_file.write_all(payload.as_bytes()).is_err() || body_file.sync_all().is_err() {
        let _ = fs::remove_file(&body_path);
        return Err("weather_proxy_failed");
    }
    drop(body_file);

    let curl_config = format!(
        "url = \"{}\"\nrequest = \"POST\"\nconnect-timeout = 5\nmax-time = 18\nmax-filesize = {}\nsilent = true\nshow-error = true\nfail = true\nheader = \"Authorization: Bearer {}\"\nheader = \"Content-Type: application/json\"\ndata-binary = \"@{}\"\n",
        curl_escape(url),
        MAX_RESPONSE_BYTES,
        curl_escape(token),
        curl_escape(&body_path.to_string_lossy())
    );
    let config_write =
        private_create(&config_path).and_then(|mut file| file.write_all(curl_config.as_bytes()));
    if config_write.is_err() {
        let _ = fs::remove_file(&body_path);
        let _ = fs::remove_dir(&runtime);
        return Err("weather_proxy_failed");
    }
    let output = Command::new("curl")
        .args(["--config", config_path.to_string_lossy().as_ref()])
        .output();
    let _ = fs::remove_file(&config_path);
    let _ = fs::remove_file(&body_path);
    let _ = fs::remove_dir(&runtime);
    let output = output.map_err(|_| "weather_proxy_unavailable")?;
    if !output.status.success() {
        return Err("weather_provider_unavailable");
    }
    if output.stdout.len() > MAX_RESPONSE_BYTES as usize {
        return Err("weather_response_too_large");
    }
    let body = output.stdout;
    let text = std::str::from_utf8(&body).map_err(|_| "invalid_weather_response")?;
    if !text.starts_with('{')
        || !text.ends_with('}')
        || !text.contains("\"timezone_offset_minutes\"")
        || !text.contains("\"attribution\":\"Source: Includes weather data from Google\"")
    {
        return Err("invalid_weather_response");
    }
    Ok(body)
}

fn private_create(path: &Path) -> io::Result<File> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    options.open(path)
}

fn curl_escape(input: &str) -> String {
    input.replace('\\', "\\\\").replace('"', "\\\"")
}

fn send_response(stream: &mut TcpStream, mut reply: Response) -> io::Result<()> {
    if reply.status == 204 {
        reply.body.clear();
        reply.file = None;
        reply.content_length = 0;
    }
    write!(
        stream,
        "HTTP/1.1 {} {}\r\nContent-Length: {}\r\nContent-Type: {}\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n",
        reply.status,
        reply.reason,
        reply.content_length,
        reply.content_type
    )?;
    for (name, value) in &reply.headers {
        write!(stream, "{name}: {value}\r\n")?;
    }
    stream.write_all(b"\r\n")?;
    if let Some(mut file) = reply.file {
        io::copy(&mut file, stream)?;
    } else {
        stream.write_all(&reply.body)?;
    }
    stream.flush()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::TcpListener;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn test_storage() -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = env::temp_dir().join(format!("tomtom-relay-test-{nonce}"));
        fs::create_dir_all(&path).unwrap();
        path
    }

    fn http_round_trip(config: Config, request: &[u8]) -> String {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            handle_connection(stream, &config).unwrap();
        });
        let mut client = TcpStream::connect(address).unwrap();
        client.write_all(request).unwrap();
        client.shutdown(std::net::Shutdown::Write).unwrap();
        let mut response = String::new();
        client.read_to_string(&mut response).unwrap();
        server.join().unwrap();
        response
    }

    fn test_config(storage: PathBuf) -> Config {
        Config {
            bind: "127.0.0.1:0".parse().unwrap(),
            storage,
            service_token: None,
            weather_token: None,
            weather_url: DEFAULT_WEATHER_URL.into(),
        }
    }

    #[test]
    fn accepts_explicit_consent_and_valid_coordinates() {
        let parsed = parse_weather_request(
            br#"{"latitude":48.8566,"longitude":2.3522,"location_sharing_enabled":true,"language_code":"en"}"#,
        )
        .unwrap();
        assert_eq!(parsed.latitude, 48.8566);
        assert_eq!(parsed.longitude, 2.3522);
        assert_eq!(parsed.language, "en");
    }

    #[test]
    fn rejects_missing_or_false_location_consent() {
        for body in [
            br#"{"latitude":1,"longitude":2}"#.as_slice(),
            br#"{"latitude":1,"longitude":2,"location_sharing_enabled":false}"#,
        ] {
            assert_eq!(
                parse_weather_request(body).unwrap_err(),
                "location_sharing_consent_required"
            );
        }
    }

    #[test]
    fn rejects_invalid_and_non_finite_coordinates() {
        for body in [
            br#"{"latitude":91,"longitude":2,"location_sharing_enabled":true}"#.as_slice(),
            br#"{"latitude":NaN,"longitude":2,"location_sharing_enabled":true}"#,
            br#"{"latitude":1e999,"longitude":2,"location_sharing_enabled":true}"#,
        ] {
            assert!(parse_weather_request(body).is_err());
        }
    }

    #[test]
    fn rejects_duplicate_unknown_or_malformed_fields() {
        for body in [
            br#"{"latitude":1,"latitude":2,"longitude":2,"location_sharing_enabled":true}"#
                .as_slice(),
            br#"{"latitude":1,"longitude":2,"location_sharing_enabled":true,"extra":1}"#,
            br#"{"latitude":1, "longitude":2,"location_sharing_enabled":true,}"#,
            br#"{"latitude":"1","longitude":2,"location_sharing_enabled":true}"#,
        ] {
            assert!(parse_weather_request(body).is_err());
        }
    }

    #[test]
    fn validates_filename_allowlist() {
        assert!(valid_filename("ubuntu-atlas.pgm"));
        assert!(valid_filename("home.ttface"));
        for name in [
            "",
            "../secret",
            ".hidden",
            "a..b",
            "foo/bar",
            "a\\b",
            "space name",
        ] {
            assert!(!valid_filename(name), "{name}");
        }
    }

    #[test]
    fn token_comparison_requires_exact_bytes() {
        assert!(constant_time_eq(b"Bearer abc", b"Bearer abc"));
        assert!(!constant_time_eq(b"Bearer abc", b"Bearer abC"));
        assert!(!constant_time_eq(b"Bearer abc", b"Bearer abc "));
    }

    #[test]
    fn escapes_curl_config_strings() {
        assert_eq!(curl_escape("ab\\cd\"ef"), "ab\\\\cd\\\"ef");
    }

    #[test]
    fn http_health_reports_weather_readiness_without_secrets() {
        let storage = test_storage();
        let reply = http_round_trip(
            test_config(storage.clone()),
            b"GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n",
        );
        assert!(reply.contains("200 OK"));
        assert!(reply.contains("\"weather_configured\":false"));
        assert!(!reply.contains("WEATHER_SERVICE_TOKEN"));
        fs::remove_dir_all(storage).unwrap();
    }

    #[test]
    fn http_asset_round_trip_is_atomic_and_filename_bounded() {
        let storage = test_storage();
        let mut config = test_config(storage.clone());
        config.service_token = Some("service-secret".into());
        let uploaded = http_round_trip(
            config.clone(),
            b"PUT /v1/files/face.ttface HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer service-secret\r\nContent-Length: 7\r\n\r\npayload",
        );
        assert!(uploaded.contains("201 Created"));
        assert_eq!(fs::read(storage.join("face.ttface")).unwrap(), b"payload");

        let downloaded = http_round_trip(
            config.clone(),
            b"GET /v1/files/face.ttface HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer service-secret\r\n\r\n",
        );
        assert!(downloaded.contains("200 OK"));
        assert!(downloaded.ends_with("payload"));

        let unauthorized = http_round_trip(
            config.clone(),
            b"GET /v1/files HTTP/1.1\r\nHost: localhost\r\n\r\n",
        );
        assert!(unauthorized.contains("401 Unauthorized"));

        let traversal = http_round_trip(
            config,
            b"GET /v1/files/%2e%2e%2fetc%2fpasswd HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer service-secret\r\n\r\n",
        );
        assert!(traversal.contains("400 Bad Request"));
        fs::remove_dir_all(storage).unwrap();
    }

    #[test]
    fn weather_endpoint_fails_closed_without_secret_or_consent() {
        let storage = test_storage();
        let mut config = test_config(storage.clone());
        let missing_secret = http_round_trip(
            config.clone(),
            b"POST /v1/weather HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 60\r\n\r\n{\"latitude\":1,\"longitude\":2,\"location_sharing_enabled\":true}",
        );
        assert!(missing_secret.contains("503 Service Unavailable"));

        config.weather_token = Some("weather-secret".into());
        let denied = http_round_trip(
            config,
            b"POST /v1/weather HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 61\r\n\r\n{\"latitude\":1,\"longitude\":2,\"location_sharing_enabled\":false}",
        );
        assert!(denied.contains("400 Bad Request"));
        assert!(denied.contains("location_sharing_consent_required"));
        fs::remove_dir_all(storage).unwrap();
    }
}

from http.server import BaseHTTPRequestHandler, HTTPServer
import socket, threading, os
from datetime import datetime

PORT = 8081
SAVE_DIR = "images_esp"
os.makedirs(SAVE_DIR, exist_ok=True)

# ---------------- Get Local IP ----------------
def get_local_ip():
    """Get the actual local IP address"""
    try:
        # Create a dummy socket to find local IP
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))  # Connect to Google DNS
        local_ip = s.getsockname()[0]
        s.close()
        return local_ip
    except Exception:
        return socket.gethostbyname(socket.gethostname())

# ---------------- UDP Discovery Server ----------------
def discovery_server():
    UDP_PORT = 8888
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("", UDP_PORT))
    
    my_ip = get_local_ip()
    print(f"🔎 Discovery server listening on UDP port {UDP_PORT}")
    print(f"📍 Server IP: {my_ip}")
    
    while True:
        try:
            data, addr = s.recvfrom(1024)
            msg = data.decode().strip()
            
            if msg == "ESP_DISCOVER":
                response = f"SERVER_IP:{my_ip}".encode()
                s.sendto(response, addr)
                print(f"📡 Discovery request from {addr[0]} → replied with {my_ip}")
                
        except Exception as e:
            print(f"⚠️ Discovery error: {e}")

# ---------------- HTTP Receiver ----------------
class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        """Override to customize logging"""
        pass  # Suppress default logs
    
    def do_POST(self):
        try:
            content_length = int(self.headers.get('Content-Length', 0))
            
            if content_length == 0:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(b"Empty content")
                print("❌ Received empty POST request")
                return
            
            # Read image data
            data = self.rfile.read(content_length)
            
            # Validate JPEG header
            if not data.startswith(b'\xff\xd8'):
                self.send_response(400)
                self.end_headers()
                self.wfile.write(b"Invalid JPEG")
                print(f"❌ Invalid image data ({len(data)} bytes)")
                return
            
            # Create filename with timestamp
            timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
            filename = f"{SAVE_DIR}/photo_{timestamp}.jpg"
            
            # Save file
            with open(filename, 'wb') as f:
                f.write(data)
            
            # Success response
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"OK")
            
            # Log success
            size_kb = len(data) / 1024
            print(f"✅ Saved {filename} ({size_kb:.1f} KB) from {self.client_address[0]}")
            
        except Exception as e:
            print(f"❌ Error saving image: {e}")
            self.send_response(500)
            self.end_headers()
            self.wfile.write(f"Server error: {e}".encode())
    
    def do_GET(self):
        """Simple status page"""
        if self.path == "/":
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.end_headers()
            
            # Count saved images
            images = [f for f in os.listdir(SAVE_DIR) if f.endswith('.jpg')]
            count = len(images)
            
            html = f"""
            <html>
            <head><title>ESP32-CAM Receiver</title>
            <style>body{{font-family:Arial;background:#222;color:#fff;padding:20px;}}</style>
            </head>
            <body>
            <h2>📷 ESP32-CAM Image Receiver</h2>
            <p><b>Server IP:</b> {get_local_ip()}</p>
            <p><b>HTTP Port:</b> {PORT}</p>
            <p><b>UDP Discovery:</b> 8888</p>
            <p><b>Images Received:</b> {count}</p>
            <p><b>Save Directory:</b> {SAVE_DIR}</p>
            <hr>
            <h3>Recent Images:</h3>
            <ul>
            """
            
            for img in sorted(images, reverse=True)[:10]:
                html += f"<li>{img}</li>"
            
            html += """
            </ul>
            </body>
            </html>
            """
            
            self.wfile.write(html.encode())
        else:
            self.send_response(404)
            self.end_headers()

# ---------------- Run Servers ----------------
if __name__ == "__main__":
    print("=" * 50)
    print("🚀 ESP32-CAM Image Receiver Starting...")
    print("=" * 50)
    
    # Start UDP discovery server in background
    threading.Thread(target=discovery_server, daemon=True).start()
    
    # Display server info
    local_ip = get_local_ip()
    print(f"\n📍 Server IP: {local_ip}")
    print(f"🌐 HTTP Server: http://{local_ip}:{PORT}")
    print(f"🔎 UDP Discovery: Port 8888")
    print(f"💾 Save Directory: {SAVE_DIR}/")
    print(f"\n✅ Ready to receive images from ESP32-CAM")
    print("=" * 50 + "\n")
    
    # Start HTTP server (blocking)
    try:
        HTTPServer(('0.0.0.0', PORT), Handler).serve_forever()
    except KeyboardInterrupt:
        print("\n\n🛑 Server stopped by user")
    except Exception as e:
        print(f"\n❌ Server error: {e}")

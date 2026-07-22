import os
from PIL import Image, ImageDraw, ImageFont

def draw_wiring_diagram():
    # Canvas setup
    width, height = 1280, 750
    img = Image.new("RGBA", (width, height), "#0f172a")  # Dark slate background
    draw = ImageDraw.Draw(img)

    # Fonts
    try:
        font_title = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 26)
        font_header = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
        font_bold = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14)
        font_small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12)
    except Exception:
        font_title = font_header = font_bold = font_small = ImageFont.load_default()

    # Title Bar
    draw.text((40, 30), "Wi-Fi Router Watchdog — Normally Closed (NC) Wiring Schematic", fill="#f8fafc", font=font_title)
    draw.text((40, 68), "Default state: Relay de-energized -> NC loop closed -> Router powered ON continuously", fill="#94a3b8", font=font_small)

    # Helper function to draw rounded box
    def draw_box(x1, y1, x2, y2, bg_color, border_color, title, title_color):
        draw.rounded_rectangle([x1, y1, x2, y2], radius=12, fill=bg_color, outline=border_color, width=2)
        draw.rectangle([x1, y1, x2, y1 + 36], fill=border_color)
        draw.text((x1 + 15, y1 + 8), title, fill=title_color, font=font_header)

    # 1. ESP Microcontroller Box
    esp_x1, esp_y1, esp_x2, esp_y2 = 50, 120, 330, 480
    draw_box(esp_x1, esp_y1, esp_x2, esp_y2, "#1e293b", "#0284c7", "ESP Microcontroller", "#ffffff")
    draw.text((esp_x1 + 15, esp_y1 + 55), "Target: ESP32 / S3 / C3 / S2 / ESP8266", fill="#cbd5e1", font=font_small)
    
    pin_y_vcc = esp_y1 + 110
    pin_y_gnd = esp_y1 + 190
    pin_y_gpio = esp_y1 + 270

    # ESP Terminals
    draw.rounded_rectangle([esp_x2 - 100, pin_y_vcc - 14, esp_x2, pin_y_vcc + 14], radius=6, fill="#ef4444")
    draw.text((esp_x2 - 85, pin_y_vcc - 8), "5V / VCC", fill="#ffffff", font=font_bold)

    draw.rounded_rectangle([esp_x2 - 100, pin_y_gnd - 14, esp_x2, pin_y_gnd + 14], radius=6, fill="#475569")
    draw.text((esp_x2 - 80, pin_y_gnd - 8), "GND", fill="#ffffff", font=font_bold)

    draw.rounded_rectangle([esp_x2 - 100, pin_y_gpio - 14, esp_x2, pin_y_gpio + 14], radius=6, fill="#eab308")
    draw.text((esp_x2 - 85, pin_y_gpio - 8), "GPIO 4", fill="#000000", font=font_bold)

    # 2. Relay Module Box
    r_x1, r_y1, r_x2, r_y2 = 450, 120, 780, 680
    draw_box(r_x1, r_y1, r_x2, r_y2, "#1e293b", "#2563eb", "5V Optocoupler Relay Module", "#ffffff")

    # Relay DC Terminals (Left side)
    draw.text((r_x1 + 15, r_y1 + 55), "[ Low-Voltage DC Control ]", fill="#60a5fa", font=font_bold)
    
    draw.rounded_rectangle([r_x1, pin_y_vcc - 14, r_x1 + 90, pin_y_vcc + 14], radius=6, fill="#ef4444")
    draw.text((r_x1 + 25, pin_y_vcc - 8), "VCC", fill="#ffffff", font=font_bold)

    draw.rounded_rectangle([r_x1, pin_y_gnd - 14, r_x1 + 90, pin_y_gnd + 14], radius=6, fill="#475569")
    draw.text((r_x1 + 25, pin_y_gnd - 8), "GND", fill="#ffffff", font=font_bold)

    draw.rounded_rectangle([r_x1, pin_y_gpio - 14, r_x1 + 90, pin_y_gpio + 14], radius=6, fill="#eab308")
    draw.text((r_x1 + 32, pin_y_gpio - 8), "IN", fill="#000000", font=font_bold)

    # Relay AC High Voltage Terminals (Right side)
    com_y = r_y1 + 420
    nc_y = r_y1 + 500
    no_y = r_y1 + 580

    draw.text((r_x1 + 15, com_y - 45), "[ High-Voltage AC Screw Terminals ]", fill="#f59e0b", font=font_bold)

    draw.rounded_rectangle([r_x2 - 90, com_y - 14, r_x2, com_y + 14], radius=6, fill="#f59e0b")
    draw.text((r_x2 - 70, com_y - 8), "COM", fill="#000000", font=font_bold)

    draw.rounded_rectangle([r_x2 - 90, nc_y - 14, r_x2, nc_y + 14], radius=6, fill="#10b981")
    draw.text((r_x2 - 62, nc_y - 8), "NC", fill="#ffffff", font=font_bold)

    draw.rounded_rectangle([r_x2 - 90, no_y - 14, r_x2, no_y + 14], radius=6, fill="#ef4444")
    draw.text((r_x2 - 62, no_y - 8), "NO", fill="#ffffff", font=font_bold)
    draw.text((r_x1 + 20, no_y - 5), "(Normally Open - Unused)", fill="#94a3b8", font=font_small)

    # 3. AC Power Source Box
    ac_x1, ac_y1, ac_x2, ac_y2 = 930, 120, 1240, 340
    draw_box(ac_x1, ac_y1, ac_x2, ac_y2, "#450a0a", "#dc2626", "110V/220V AC Mains", "#ffffff")

    acl_y = ac_y1 + 90
    acn_y = ac_y1 + 170

    draw.rounded_rectangle([ac_x1, acl_y - 14, ac_x1 + 120, acl_y + 14], radius=6, fill="#dc2626")
    draw.text((ac_x1 + 12, acl_y - 8), "AC Line (L)", fill="#ffffff", font=font_bold)

    draw.rounded_rectangle([ac_x1, acn_y - 14, ac_x1 + 130, acn_y + 14], radius=6, fill="#0284c7")
    draw.text((ac_x1 + 10, acn_y - 8), "AC Neutral (N)", fill="#ffffff", font=font_bold)

    # 4. Router Power Supply Box
    rt_x1, rt_y1, rt_x2, rt_y2 = 930, 420, 1240, 680
    draw_box(rt_x1, rt_y1, rt_x2, rt_y2, "#1e1b4b", "#6366f1", "Router Power Adapter", "#ffffff")

    rtl_y = nc_y
    rtn_y = rt_y1 + 170

    draw.rounded_rectangle([rt_x1, rtl_y - 14, rt_x1 + 130, rtl_y + 14], radius=6, fill="#10b981")
    draw.text((rt_x1 + 12, rtl_y - 8), "Input Line (L)", fill="#ffffff", font=font_bold)

    draw.rounded_rectangle([rt_x1, rtn_y - 14, rt_x1 + 140, rtn_y + 14], radius=6, fill="#0284c7")
    draw.text((rt_x1 + 10, rtn_y - 8), "Input Neutral (N)", fill="#ffffff", font=font_bold)

    draw.text((rt_x1 + 15, rt_y1 + 220), "⚡ DC Out -> Router", fill="#a5b4fc", font=font_bold)

    # --- Draw Wires & Clean Labels ---

    # 1. 5V Wire (Red)
    draw.line([(esp_x2, pin_y_vcc), (r_x1, pin_y_vcc)], fill="#ef4444", width=3)
    draw.text((338, pin_y_vcc - 20), "5V DC Power Wire", fill="#fca5a5", font=font_small)

    # 2. GND Wire (Slate Gray)
    draw.line([(esp_x2, pin_y_gnd), (r_x1, pin_y_gnd)], fill="#94a3b8", width=3)
    draw.text((338, pin_y_gnd - 20), "Ground Wire (GND)", fill="#cbd5e1", font=font_small)

    # 3. GPIO Wire (Yellow)
    draw.line([(esp_x2, pin_y_gpio), (r_x1, pin_y_gpio)], fill="#eab308", width=3)
    draw.text((338, pin_y_gpio - 20), "GPIO 4 Control Signal", fill="#fde047", font=font_small)

    # 4. AC Line Wire to COM (Amber / Orange - High Voltage)
    draw.line([(ac_x1, acl_y), (830, acl_y), (830, com_y), (r_x2, com_y)], fill="#f59e0b", width=4)
    draw.text((838, acl_y - 24), "AC Line Hot (L)", fill="#fbbf24", font=font_bold)

    # 5. NC Wire to Router Line (Green - High Voltage NC Loop)
    draw.line([(r_x2, nc_y), (rt_x1, nc_y)], fill="#10b981", width=5)
    draw.text((785, nc_y - 24), "NC Power Loop", fill="#34d399", font=font_bold)

    # 6. AC Neutral Wire to Router Neutral (Cyan - High Voltage)
    draw.line([(ac_x1, acn_y), (860, acn_y), (860, rtn_y), (rt_x1, rtn_y)], fill="#0284c7", width=4)
    draw.text((868, rtn_y - 24), "Direct AC Neutral (N)", fill="#38bdf8", font=font_bold)

    # Footer safety note
    draw.text((40, 715), "🛡️ Safety Notice: Install a 2A inline fuse on AC Line Hot. Ensure proper wire insulation between DC low-voltage and AC high-voltage.", fill="#64748b", font=font_small)

    # Save PNG image
    out_dir = "/home/ubuntu/.gemini/antigravity-ide/scratch/wifi-router-watchdog/docs/images"
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "nc_wiring_diagram.png")
    img.save(out_path)
    print(f"Saved refined wiring diagram image to {out_path}")

if __name__ == "__main__":
    draw_wiring_diagram()

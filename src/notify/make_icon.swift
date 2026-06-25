// Generates a Remindr.iconset directory suitable for iconutil.
// Usage: swift make_icon.swift <output.iconset>
import Cocoa

guard CommandLine.arguments.count >= 2 else {
    fputs("Usage: make_icon.swift <output.iconset>\n", stderr)
    exit(1)
}

let outDir = CommandLine.arguments[1]
try? FileManager.default.createDirectory(atPath: outDir, withIntermediateDirectories: true)

func renderIcon(pixels: Int) -> Data? {
    let dim = CGFloat(pixels)
    guard let bitmap = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: pixels, pixelsHigh: pixels,
        bitsPerSample: 8, samplesPerPixel: 4,
        hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0, bitsPerPixel: 0
    ), let ctx = NSGraphicsContext(bitmapImageRep: bitmap) else { return nil }

    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = ctx

    // Dark charcoal background (#1E1E1E)
    NSColor(red: 0x1E/255, green: 0x1E/255, blue: 0x1E/255, alpha: 1).setFill()
    NSBezierPath(roundedRect: NSRect(x: 0, y: 0, width: dim, height: dim),
                 xRadius: dim * 0.226, yRadius: dim * 0.226).fill()

    // Golden yellow R (#FFCC00)
    let font = NSFont(name: "HelveticaNeue-Bold", size: dim * 0.64)
               ?? NSFont.boldSystemFont(ofSize: dim * 0.64)
    let attrs: [NSAttributedString.Key: Any] = [
        .font: font,
        .foregroundColor: NSColor(red: 1.0, green: 0.8, blue: 0.0, alpha: 1),
    ]
    let str = NSAttributedString(string: "R", attributes: attrs)
    let sz  = str.size()
    str.draw(at: NSPoint(x: (dim - sz.width) / 2, y: (dim - sz.height) / 2))

    NSGraphicsContext.restoreGraphicsState()
    return bitmap.representation(using: .png, properties: [:])
}

// iconset entries: (pointSize, scale) — pixel size = points * scale
let entries: [(Int, Int)] = [
    (16, 1), (16, 2),
    (32, 1), (32, 2),
    (128, 1), (128, 2),
    (256, 1), (256, 2),
    (512, 1), (512, 2),
]

var cache: [Int: Data] = [:]

for (pts, scale) in entries {
    let px = pts * scale
    let png: Data
    if let cached = cache[px] {
        png = cached
    } else if let rendered = renderIcon(pixels: px) {
        cache[px] = rendered
        png = rendered
    } else {
        fputs("make_icon: render failed at \(px)px\n", stderr)
        continue
    }
    let name = scale == 1 ? "icon_\(pts)x\(pts).png" : "icon_\(pts)x\(pts)@2x.png"
    do {
        try png.write(to: URL(fileURLWithPath: "\(outDir)/\(name)"))
    } catch {
        fputs("make_icon: write failed for \(name): \(error)\n", stderr)
    }
}

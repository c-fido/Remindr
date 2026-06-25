import Cocoa
import UserNotifications

let args = CommandLine.arguments
guard args.count >= 3 else {
    fputs("Usage: remind-notify <title> <body>\n", stderr)
    exit(1)
}

let notifTitle = args[1]
let notifBody  = args[2]

let home = ProcessInfo.processInfo.environment["HOME"] ?? "/tmp"

func readConfig() -> [String: Any]? {
    let path = "\(home)/.config/reminderd/config.json"
    guard let data = try? Data(contentsOf: URL(fileURLWithPath: path)),
          let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    else { return nil }
    return json
}

func resolveSound(cfg: [String: Any]?) -> UNNotificationSound {
    if let src = cfg?["sound_file"] as? String,
       FileManager.default.fileExists(atPath: src) {
        let soundsDir = "\(home)/Library/Sounds"
        try? FileManager.default.createDirectory(atPath: soundsDir, withIntermediateDirectories: true)
        let ext  = URL(fileURLWithPath: src).pathExtension
        let name = "remindr-custom.\(ext.isEmpty ? "caf" : ext)"
        let dest = "\(soundsDir)/\(name)"
        try? FileManager.default.removeItem(atPath: dest)
        if (try? FileManager.default.copyItem(atPath: src, toPath: dest)) != nil {
            return UNNotificationSound(named: UNNotificationSoundName(name))
        }
        fputs("remind-notify: custom sound copy failed — using system default\n", stderr)
    }
    return UNNotificationSound.default
}

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ note: Notification) {
        let center = UNUserNotificationCenter.current()
        let cfg    = readConfig()
        let soundEnabled = cfg?["sound_enabled"] as? Bool ?? true

        center.requestAuthorization(options: [.alert, .sound]) { granted, error in
            if let err = error {
                fputs("remind-notify: auth error: \(err.localizedDescription)\n", stderr)
                DispatchQueue.main.async { NSApp.terminate(nil) }
                return
            }
            guard granted else {
                fputs("remind-notify: permission denied — open System Settings > Notifications and enable Remindr\n", stderr)
                DispatchQueue.main.async { NSApp.terminate(nil) }
                return
            }

            let content   = UNMutableNotificationContent()
            content.title = notifTitle
            content.body  = notifBody
            if soundEnabled { content.sound = resolveSound(cfg: cfg) }

            let req = UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil)
            center.add(req) { err in
                if let err = err {
                    fputs("remind-notify: add request failed: \(err.localizedDescription)\n", stderr)
                }
                DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { NSApp.terminate(nil) }
            }
        }
    }
}

let app      = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()

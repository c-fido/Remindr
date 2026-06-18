import Cocoa
import UserNotifications

let args = CommandLine.arguments
guard args.count >= 3 else {
    fputs("Usage: remind-notify <title> <body>\n", stderr)
    exit(1)
}

let notifTitle = args[1]
let notifBody  = args[2]

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ note: Notification) {
        let center = UNUserNotificationCenter.current()

        center.requestAuthorization(options: [.alert, .sound]) { granted, error in
            if let err = error {
                fputs("remind-notify: auth error: \(err.localizedDescription)\n", stderr)
                DispatchQueue.main.async { NSApp.terminate(nil) }
                return
            }
            guard granted else {
                fputs("remind-notify: permission denied — open System Settings > Notifications and enable Redmindr\n", stderr)
                DispatchQueue.main.async { NSApp.terminate(nil) }
                return
            }

            let content   = UNMutableNotificationContent()
            content.title = notifTitle
            content.body  = notifBody
            content.sound = .default

            let req = UNNotificationRequest(
                identifier: UUID().uuidString, content: content, trigger: nil)

            center.add(req) { err in
                if let err = err {
                    fputs("remind-notify: \(err.localizedDescription)\n", stderr)
                }
                // Brief pause so the notification centre can deliver before we exit.
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                    NSApp.terminate(nil)
                }
            }
        }
    }
}

let app      = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()

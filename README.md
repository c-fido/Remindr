# Remindr

A lightweight desktop notification scheduler for macOS and Linux.

Two binaries and one helper ship together:

| Artifact          | Role                                                          |
|-------------------|---------------------------------------------------------------|
| `reminderd`       | Background daemon — watches timers, fires notifications       |
| `remind`          | CLI — add, list, and delete reminders                         |
| `Remindr.app`    | macOS notification helper (proper Notification Center entry)  |

---

## Install

### Requirements

| Tool             | Version        | Notes                                            |
|------------------|----------------|--------------------------------------------------|
| CMake            | ≥ 3.14         |                                                  |
| C++ compiler     | C++17          | Clang or GCC                                     |
| Swift compiler   | any            | macOS only; provides native notification support |
| Internet         | first build    | fetches `nlohmann/json` automatically            |

On macOS the Xcode Command Line Tools provide Clang and `swiftc`:

```sh
xcode-select --install
brew install cmake           # if you don't have cmake yet
```

On Debian/Ubuntu:

```sh
sudo apt install cmake g++
```

### Build

```sh
git clone https://github.com/c-fido/Remindr
cd remind
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

**Clean build** (wipes the build directory and starts fresh):

```sh
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

### Install binaries

Install to `~/.local/bin` (no `sudo` required, recommended):

```sh
cmake --install build --prefix ~/.local
```

On macOS, also codesign the notification helper so the system accepts it:

```sh
codesign -s - --deep --force ~/.local/bin/Remindr.app
```

Or system-wide:

```sh
sudo cmake --install build   # installs to /usr/local/bin
sudo codesign -s - --deep --force /usr/local/bin/Remindr.app
```

Make sure the install directory is on your `$PATH`:

**fish**
```sh
fish_add_path ~/.local/bin
```

**zsh / bash** — add to `~/.zshrc` or `~/.bashrc`:
```sh
export PATH="$HOME/.local/bin:$PATH"
```

### Register as a startup service

Run **without `sudo`** as your normal login user:

```sh
remind --install
```

- **macOS** — writes a launchd plist to `~/Library/LaunchAgents/` and attempts to start `reminderd` immediately via `launchctl bootstrap`. If that fails the plist is still installed and `reminderd` will start on next login. You can also start it manually:

  ```sh
  launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.remind.reminderd.plist
  ```

  > **Note:** do not run `remind --install` with `sudo` — the daemon must be registered in your user's GUI session, not root's.

- **Linux** — writes a systemd user service to `~/.config/systemd/user/`. Enable it with:

  ```sh
  systemctl --user daemon-reload
  systemctl --user enable --now reminderd
  ```

Verify the daemon is running:

```sh
# macOS
launchctl list com.remind.reminderd

# Linux
systemctl --user status reminderd
```

### Grant notification permission (macOS)

The first time a reminder fires, macOS will prompt you to allow notifications from **Remindr**. You can also trigger this manually:

```sh
~/.local/bin/Remindr.app/Contents/MacOS/remind-notify Remindr "Notifications enabled!"
```

After approving, **Remindr** appears in **System Settings → Notifications** where you can control the alert style.

---

## Usage

### Add a reminder

No quotes needed — time tokens are detected automatically from the end of the command:

```sh
remind coffee chat friday 3pm
remind call mom in 2 hours
remind dentist appointment tuesday at 10:30am
remind standup tomorrow 9am --daily
remind team meeting monday at 10am --weekly
```

Quoted messages also work if you prefer:

```sh
remind "take out the trash" tomorrow 8pm
```

#### Time expression reference

| Pattern               | Example              |
|-----------------------|----------------------|
| `in N minutes`        | `in 45 minutes`      |
| `in N hours`          | `in 2 hours`         |
| `in N days`           | `in 3 days`          |
| `in N weeks`          | `in 1 week`          |
| `tomorrow`            | `tomorrow`           |
| `tomorrow <time>`     | `tomorrow 9am`       |
| `<weekday>`           | `friday`             |
| `<weekday> at <time>` | `friday at 4pm`      |
| `<time>`              | `16:00` / `4:30pm`   |

Time formats accepted: `4pm`, `4:30pm`, `16:00`, `9am`. All day and time keywords are case-insensitive.

Weekday reminders always fire on the **next** occurrence of that day (never today).
Clock-only reminders fire today if the time hasn't passed yet, or tomorrow if it has.

#### Recurrence flags

| Flag       | Behaviour                   |
|------------|-----------------------------|
| `--daily`  | Re-fires every 24 hours     |
| `--weekly` | Re-fires every 7 days       |

### List upcoming reminders

```sh
remind list
```

### Delete a reminder

```sh
remind delete <id>
```

Use the ID shown by `remind list`.

### Notification sound

Toggle the sound on or off:

```sh
remind sound on
remind sound off
```

Use a custom sound file (`.caf`, `.aiff`, `.wav`, or `.mp3`):

```sh
remind sound set ~/Downloads/my-sound.caf
```

The file is copied into `~/.config/reminderd/` so you can move or delete the original afterwards.

Revert to the system default notification sound:

```sh
remind sound reset
```

---

## How it works

`reminderd` runs a `select()`-based event loop with a 1-second tick. On each tick it checks whether any reminder's `fire_at` timestamp has passed and, if so, fires a native notification:

| Platform | Notification mechanism                                                  |
|----------|-------------------------------------------------------------------------|
| macOS    | `Remindr.app` Swift helper via `UNUserNotificationCenter` — displays the Remindr icon and plays the system default sound (or a custom sound if set). Falls back to `osascript` if the helper is missing. |
| Linux    | `notify-send`                                                           |

`remind` connects to the daemon over a Unix domain socket (`/tmp/reminderd.sock`) and sends newline-delimited JSON commands (`ADD`, `LIST`, `DELETE`).

Reminders are persisted to disk on every change. If the file is corrupt at startup it is backed up and a fresh store is created.

---

## File locations

| Path                                                    | Purpose                                       |
|---------------------------------------------------------|-----------------------------------------------|
| `~/.config/reminderd/reminders.json`                    | Persistent reminder store                     |
| `~/.config/reminderd/config.json`                       | Sound on/off flag and custom sound path       |
| `~/.config/reminderd/custom-sound.<ext>`                | Custom sound file (copied by `remind sound set`) |
| `~/.config/reminderd/reminderd.log`                     | Daemon log (autostart only)                   |
| `/tmp/reminderd.sock`                                   | Unix domain socket                            |
| `~/.local/bin/Remindr.app`                             | macOS notification helper (installed with `cmake --install`) |
| `~/Library/LaunchAgents/com.remind.reminderd.plist`     | macOS autostart (created by `remind --install`) |
| `~/.config/systemd/user/reminderd.service`              | Linux autostart (created by `remind --install`) |

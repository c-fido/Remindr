#pragma once
#include <optional>
#include <string>

// Returns a connected fd, or -1 on failure.
int  connect_to_daemon(const std::string& path);

// Send a newline-terminated string and receive a newline-terminated response.
// Returns the response string (without the trailing newline), or nullopt on error.
std::optional<std::string> send_command(int fd, const std::string& payload);

// Read one newline-delimited message from fd into out. Returns false on EOF/error.
bool read_line(int fd, std::string& out);

// Write a newline-terminated string to fd. Returns false on error.
bool write_line(int fd, const std::string& payload);

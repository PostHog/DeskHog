import AppKit

// Entry point. main.swift allows top-level code, so we wire up NSApplication
// with our delegate and run the event loop.
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()

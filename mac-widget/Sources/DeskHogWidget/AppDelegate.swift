import AppKit
import SwiftUI

/// Sets up the floating ornament window and a menu-bar item to quit.
final class AppDelegate: NSObject, NSApplicationDelegate {
    private let engine = HogEngine()
    private var window: NSWindow?
    private var statusItem: NSStatusItem?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Accessory app: no Dock icon, lives in the menu bar / on the desktop.
        NSApp.setActivationPolicy(.accessory)

        engine.start()

        let hosting = NSHostingView(rootView: HogView(engine: engine))
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 150, height: 175),
            styleMask: [.borderless],
            backing: .buffered,
            defer: false
        )
        window.isOpaque = false
        window.backgroundColor = .clear
        window.hasShadow = true
        window.level = .floating                       // sits above normal windows
        window.isMovableByWindowBackground = true      // drag from anywhere
        window.collectionBehavior = [.canJoinAllSpaces, .stationary]
        window.contentView = hosting
        window.setContentSize(hosting.fittingSize)
        window.center()
        window.makeKeyAndOrderFront(nil)
        self.window = window

        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        item.button?.title = "🦔"
        let menu = NSMenu()
        menu.addItem(NSMenuItem(title: "Quit DeskHog Widget",
                                action: #selector(NSApplication.terminate(_:)),
                                keyEquivalent: "q"))
        item.menu = menu
        self.statusItem = item
    }

    func applicationWillTerminate(_ notification: Notification) {
        engine.stop()
    }
}

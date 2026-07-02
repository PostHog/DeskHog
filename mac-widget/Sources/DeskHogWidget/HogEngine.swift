import SwiftUI

/// Drives the widget: samples CPU once a second, smooths it, and advances the
/// wheel angle every frame so the hog runs faster the busier the Mac is.
final class HogEngine: ObservableObject {
    /// Wheel rotation in degrees (published ~60x/sec to animate the view).
    @Published var angle: Double = 0
    /// Smoothed CPU load, 0-100.
    @Published var cpu: Double = 0

    // Run-speed mapping: revolutions per second at idle vs. pegged CPU.
    private let idleRPS = 0.25
    private let maxRPS = 4.0

    private let sampler = CPUSampler()
    private var timer: Timer?
    private var lastTick = Date()
    private var lastSample = Date.distantPast

    func start() {
        lastTick = Date()
        // 60 fps is plenty for a desk ornament and stays cheap.
        let timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            self?.tick()
        }
        // Keep spinning while menus are open / the app is otherwise busy.
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    private func tick() {
        let now = Date()
        let dt = now.timeIntervalSince(lastTick)
        lastTick = now

        if now.timeIntervalSince(lastSample) >= 1.0 {
            lastSample = now
            let raw = sampler.usage()          // 0-100
            cpu += (raw - cpu) * 0.4           // light smoothing
        }

        let rps = idleRPS + (cpu / 100.0) * (maxRPS - idleRPS)
        angle += rps * 360.0 * dt
        if angle >= 360 { angle = angle.truncatingRemainder(dividingBy: 360) }
    }
}

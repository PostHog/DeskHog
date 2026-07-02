import SwiftUI

/// The visible ornament: a hedgehog running inside a spinning wheel, with a
/// CPU% readout. Drag it anywhere; it floats above other windows.
struct HogView: View {
    @ObservedObject var engine: HogEngine

    var body: some View {
        VStack(spacing: 6) {
            ZStack {
                Wheel()
                    .stroke(Color.white.opacity(0.85), lineWidth: 3)
                    .rotationEffect(.degrees(engine.angle))
                    .frame(width: 110, height: 110)

                // The hog bobs up and down in time with the wheel to read as running.
                Text("🦔")
                    .font(.system(size: 46))
                    .offset(y: 14 + bob)
            }
            .frame(width: 120, height: 120)

            Text("CPU \(Int(engine.cpu.rounded()))%")
                .font(.system(size: 15, weight: .heavy, design: .rounded))
                .foregroundColor(.white)
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .fill(Color(red: 0.055, green: 0.478, blue: 0.0))  // PostHog-ish green
        )
    }

    /// Small vertical bob derived from the wheel angle to fake a running gait.
    private var bob: CGFloat {
        CGFloat(sin(engine.angle * .pi / 180.0 * 2)) * 3
    }
}

/// A wheel: outer rim plus spokes, so rotation is visible.
struct Wheel: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        let center = CGPoint(x: rect.midX, y: rect.midY)
        let radius = min(rect.width, rect.height) / 2

        path.addEllipse(in: CGRect(x: center.x - radius, y: center.y - radius,
                                   width: radius * 2, height: radius * 2))

        let spokes = 8
        for i in 0..<spokes {
            let a = Double(i) / Double(spokes) * 2 * .pi
            path.move(to: center)
            path.addLine(to: CGPoint(x: center.x + CGFloat(cos(a)) * radius,
                                     y: center.y + CGFloat(sin(a)) * radius))
        }
        return path
    }
}

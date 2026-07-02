// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "DeskHogWidget",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "DeskHogWidget",
            path: "Sources/DeskHogWidget"
        )
    ]
)

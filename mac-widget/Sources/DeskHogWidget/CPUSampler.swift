import Darwin

/// Reads overall system CPU usage (0-100%) via the Mach host_statistics API.
///
/// Each call diffs the cumulative CPU tick counters against the previous call,
/// so usage reflects the interval between samples. The first call has no prior
/// baseline and returns 0.
final class CPUSampler {
    private var previous: host_cpu_load_info_data_t?

    func usage() -> Double {
        var count = mach_msg_type_number_t(
            MemoryLayout<host_cpu_load_info_data_t>.size / MemoryLayout<integer_t>.size)
        var info = host_cpu_load_info_data_t()

        let kr = withUnsafeMutablePointer(to: &info) { ptr -> kern_return_t in
            ptr.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { intPtr in
                host_statistics(mach_host_self(), host_flavor_t(HOST_CPU_LOAD_INFO), intPtr, &count)
            }
        }
        guard kr == KERN_SUCCESS else { return 0 }

        defer { previous = info }
        guard let prev = previous else { return 0 }

        // cpu_ticks is indexed by CPU_STATE_*: 0=USER, 1=SYSTEM, 2=IDLE, 3=NICE.
        let user   = Double(info.cpu_ticks.0 &- prev.cpu_ticks.0)
        let system = Double(info.cpu_ticks.1 &- prev.cpu_ticks.1)
        let idle   = Double(info.cpu_ticks.2 &- prev.cpu_ticks.2)
        let nice   = Double(info.cpu_ticks.3 &- prev.cpu_ticks.3)

        let used = user + system + nice
        let total = used + idle
        guard total > 0 else { return 0 }
        return max(0, min(100, used / total * 100.0))
    }
}

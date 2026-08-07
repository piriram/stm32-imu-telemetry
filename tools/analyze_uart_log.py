import sys

def analyze_log(filepath):
    valid_records = 0
    parsing_failures = 0
    sequences = []
    timestamps = []
    statuses = {}
    
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            
            parts = line.split(',')
            if len(parts) >= 12 and parts[0] == 'IMU':
                try:
                    seq = int(parts[1])
                    t_ms = int(parts[2])
                    status = parts[11]
                    
                    sequences.append(seq)
                    timestamps.append(t_ms)
                    statuses[status] = statuses.get(status, 0) + 1
                    valid_records += 1
                except ValueError:
                    parsing_failures += 1
            else:
                parsing_failures += 1
                
    if not sequences:
        print("No valid IMU records found.")
        return
        
    first_seq = sequences[0]
    last_seq = sequences[-1]
    
    # Calculate gaps
    gaps = 0
    gap_locations = []
    for i in range(1, len(sequences)):
        if sequences[i] - sequences[i-1] != 1:
            gaps += (sequences[i] - sequences[i-1] - 1)
            gap_locations.append((sequences[i-1], sequences[i]))
            
    # Calculate timestamp intervals
    intervals = []
    for i in range(1, len(timestamps)):
        intervals.append(timestamps[i] - timestamps[i-1])
        
    min_interval = min(intervals) if intervals else 0
    max_interval = max(intervals) if intervals else 0
    avg_interval = sum(intervals) / len(intervals) if intervals else 0
    
    print("=== UART 60s Telemetry Analysis ===")
    print(f"- Valid IMU Records: {valid_records}")
    print(f"- Parsing Failures: {parsing_failures}")
    print(f"- First Sequence: {first_seq}")
    print(f"- Last Sequence: {last_seq}")
    print(f"- Sequence Gaps: {gaps}")
    for start, end in gap_locations:
        print(f"  Gap between seq {start} and {end}")
    
    print(f"- Interval Minimum: {min_interval} ms")
    print(f"- Interval Average: {avg_interval:.1f} ms")
    print(f"- Interval Maximum: {max_interval} ms")
    print("- Status Distribution:")
    for k, v in statuses.items():
        print(f"  {k}: {v}")

if __name__ == '__main__':
    log_file = 'docs/validation/uart_60s_session.txt'
    if len(sys.argv) > 1:
        log_file = sys.argv[1]
    analyze_log(log_file)

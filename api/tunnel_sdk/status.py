from .errors import ProtocolError


def decode_status(r):
    if len(r) != 96 or any(type(v) is not int or not 0 <= v <= 65535 for v in r):
        raise ProtocolError("Expected 96 unsigned 16-bit registers")
    if r[0] not in (2, 3):
        raise ProtocolError("This SDK requires register map 2 or 3")
    return dict(
        registers=list(r),
        map_version=r[0],
        firmware=r[1],
        state=r[2],
        fault=r[3],
        mode=r[4],
        inputs={f"IN{i+1}": bool(r[5] & (1 << i)) for i in range(7)},
        activity_flags=r[6],
        stop_reason=r[7],
        stop_flags=r[8],
        speed=r[9],
        unique_epcs=r[10],
        total_reads=(r[11] << 16) | r[12],
        scan_elapsed_ms=(r[13] << 16) | r[14],
        reader_flags=r[15],
        owner=r[16],
        lease_ms=r[17],
        last_sequence=r[18],
        last_result=r[19],
        reader_state=r[20],
        reader_sequence=r[21],
        reader_transport_status=r[22],
        reader_reply=r[32 : 32 + min(r[23], 64)],
        ethernet_flags=r[24],
        ip=".".join(map(str, (r[25] >> 8, r[25] & 255, r[26] >> 8, r[26] & 255))),
        rtu_ready=bool(r[29]),
        uptime_ms=(r[30] << 16) | r[31],
    )

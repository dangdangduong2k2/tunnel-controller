class ProtocolError(RuntimeError):
    pass


class UnknownOutcome(ProtocolError):
    """Transport/ACK failed: read state; never blindly retry a motion command."""

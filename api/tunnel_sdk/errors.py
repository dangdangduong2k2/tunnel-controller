class ProtocolError(RuntimeError):
    pass


class ReaderError(ProtocolError):
    """Matching reader transaction failed transport, validation or reader status."""


class UnknownOutcome(ProtocolError):
    """Transport/ACK failed: read state; never blindly retry a motion command."""

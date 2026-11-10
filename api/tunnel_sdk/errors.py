class ProtocolError(RuntimeError):
    pass


class BusyError(ProtocolError):
    """Device rejected a request due to current state/ownership; never auto-retry."""

    def __init__(
        self,
        message="BUSY: current state does not permit this request",
        *,
        result=None,
        exception=None,
        code=None,
    ):
        super().__init__(message)
        self.result = result
        self.exception = exception
        self.code = code


def require_accepted(ack):
    """Validate an acceptance dictionary; does not wait for execution completion.

    Command methods retain their existing dictionary return contract. Use this
    helper when exception-based handling of application result 2 is preferred.
    """
    if ack.get("result") == 2:
        raise BusyError(result=2, exception=ack.get("exception"), code=ack.get("code"))
    if not ack.get("accepted"):
        raise ProtocolError(f"Device rejected command: {ack}")
    return ack


class ReaderError(ProtocolError):
    """Matching reader transaction failed transport, validation or reader status."""


class UnknownOutcome(ProtocolError):
    """Transport/ACK failed: read state; never blindly retry a motion command."""

from .operations import OPERATIONS, CATALOG


def crc16(data):
    value = 65535
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
    return value


def words(values):
    values = list(values)
    if len(values) > 6 or any(type(v) is not int or not 0 <= v <= 65535 for v in values):
        raise ValueError("args: at most six unsigned 16-bit integers")
    return values + [0] * (6 - len(values))


class Commands:
    def call(self, method, **params):
        values = named_words(method, params)
        return self.execute(OPERATIONS[method], values)

    def command(self, action, args=()):
        if action in ("claim", "heartbeat", "release", "stop", "emergency"):
            if args:
                raise ValueError("This action takes no arguments")
            return getattr(self, action)()
        if action not in OPERATIONS:
            raise ValueError("Unknown action")
        return self.execute(OPERATIONS[action], args)


def named_words(method, params):
    if method not in CATALOG:
        raise ValueError("Unknown method")
    spec = CATALOG[method]
    if set(params) != {p[0] for p in spec["params"]}:
        raise ValueError("Expected params: " + ", ".join(p[0] for p in spec["params"]))
    values = []
    for name, minimum, maximum in spec["params"]:
        value = params[name]
        if type(value) is not int or not minimum <= value <= maximum:
            raise ValueError(f"{name}: expected integer {minimum}..{maximum}")
        values.append(value)
    return values

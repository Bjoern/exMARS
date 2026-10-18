# This is the Python equivalent of Warrior.rb

class Converter:
    """Converts from element to array index and vice versa."""
    def __init__(self, array):
        self._arr = array
        self._ref = {val: i for i, val in enumerate(array)}

    def encode(self, string):
        return self._ref.get(string.upper())

    def decode(self, value):
        return self._arr[value]

    def __getitem__(self, key):
        if isinstance(key, str):
            return self._ref.get(key.upper())
        elif isinstance(key, int):
            return self._arr[key]
        return None

# These converters match the ones in Warrior.rb
OPCODE_CONV = Converter([
    "DAT", "SPL", "MOV", "DJN", "ADD", "JMZ", 
    "SUB", "SEQ", "SNE", "SLT", "JMN", "JMP", 
    "NOP", "MUL", "MOD", "DIV", "LDP", "STP" 
])

OPCODE_MOD_CONV = Converter(["F", "A", "B", "AB", "BA", "X", "I"])

ADDR_MOD_CONV = Converter(["$", "#", "@", "<", ">", "*", "{", "}"])


class Instruction:
    """One Corewar instruction."""
    def __init__(self, ins, modif, addra, numa, addrb, numb):
        # The C extension will read this 'data' attribute.
        # It must store the *integer* values, just like the Ruby version.
        self.data = [
            OPCODE_CONV[ins],
            OPCODE_MOD_CONV[modif],
            ADDR_MOD_CONV[addra],
            int(numa),
            ADDR_MOD_CONV[addrb],
            int(numb)
        ]

    def __getitem__(self, x):
        return self.data[x]

    def __str__(self):
        return "{:3s}.{:<2s} {}{:>6d}, {}{:>6d}".format(
            OPCODE_CONV[self.data[0]],
            OPCODE_MOD_CONV[self.data[1]],
            ADDR_MOD_CONV[self.data[2]],
            self.data[3],
            ADDR_MOD_CONV[self.data[4]],
            self.data[5]
        )

class Warrior:
    """A corewars individual."""
    def __init__(self):
        self.instructions = []
        self.startPos = 0
        self.name = "Unknown"
        self.authorName = "Anonymous"
        self.fileName = None
        self.pin = None  # PIN number; warriors with equal PINs share P-space
        self.id = None

    def __getitem__(self, x):
        return self.instructions[x]

    def __str__(self):
        s = f'Program "{self.name}" (length {len(self.instructions)}) by "{self.authorName}"\n\n'
        s += f'{"":<6s} {"ORG":>3s}{"":>3s}  {"START":>6s}\n'
        for i, instr in enumerate(self.instructions):
            start_label = "START" if i == self.startPos else ""
            s += f'{start_label:<6s} {instr}\n'
        return s
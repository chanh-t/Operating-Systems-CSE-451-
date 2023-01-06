
class DecodeBT(gdb.Command):

    def __init__(self):
        super(DecodeBT, self).__init__("line", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        self.dont_repeat()
        for arg in args.split():
            if arg == '0':
                continue

            addrstr = arg if arg.startswith('0x') else f'0x{arg.zfill(8).rjust(16, "f")}'
            addr = int(addrstr, 16)
            symtab = gdb.find_pc_line(addr)
            block = gdb.block_for_pc(addr)
            while block and not block.function:
                block = block.superblock

            if block and block.function:
                name = block.function.print_name
            else:
                name = "No executable C code"

            if symtab.symtab:
                filename = symtab.symtab.filename
            else:
                filename = "unknown"

            print(f'0x{addr:x}: {filename}:{symtab.line}: {name}')

DecodeBT()
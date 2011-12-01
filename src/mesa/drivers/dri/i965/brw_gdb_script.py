import gdb.printing

class gl_texture_image_printer:
    "Print a gl_texture_image for the intel driver"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "asdfasdfasdf"

class gl_renderbuffer_printer:
    "Print a gl_texture_image for the intel driver"

    def __init__(self, val):
        print "rb init"
        self.val = val

    def to_string(self):
        print "rb string"
        return "asdfasdfasdf"

printer = gdb.printing.RegexpCollectionPrettyPrinter("i965_dri.so")
printer.add_printer("struct gl_texture_image", "^struct gl_texture_image$",
                    gl_texture_image_printer)
printer.add_printer("struct gl_renderbuffer", "^struct gl_renderbuffer$",
                    gl_renderbuffer_printer)
gdb.printing.register_pretty_printer(gdb.current_objfile(), printer)

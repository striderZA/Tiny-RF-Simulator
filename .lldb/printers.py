import lldb


class MSVCVectorProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def update(self):
        try:
            pair = self.valobj.GetChildMemberWithName("_Mypair")
            myval2 = pair.GetChildMemberWithName("_Myval2")

            # Extract the pointer members
            first = myval2.GetChildMemberWithName("_Myfirst")
            last = myval2.GetChildMemberWithName("_Mylast")

            # Some MSVC versions store pointers as intptr_t, not T*
            first_val = int(first.GetValue(), 16)
            last_val = int(last.GetValue(), 16)

            ttype = first.GetType().GetPointeeType()
            elem_size = ttype.GetByteSize()

            if elem_size == 0:
                self.size = 0
            else:
                self.size = max((last_val - first_val) // elem_size, 0)

            self.first = first
            self.elem_type = ttype

        except:
            self.size = 0

    def num_children(self):
        return self.size

    def get_child_at_index(self, index):
        if index < 0 or index >= self.size:
            return None
        offset = index * self.elem_type.GetByteSize()
        try:
            return self.first.CreateChildAtOffset(f"[{index}]", offset, self.elem_type)
        except:
            return None

    def get_child_index(self, name):
        try:
            idx = int(name.strip("[]"))
            if 0 <= idx < self.size:
                return idx
            return -1
        except:
            return -1


def vector_summary(valobj, internal):
    try:
        pair = valobj.GetChildMemberWithName("_Mypair")
        mv = pair.GetChildMemberWithName("_Myval2")
        first = mv.GetChildMemberWithName("_Myfirst")
        last = mv.GetChildMemberWithName("_Mylast")

        first_val = int(first.GetValue(), 16)
        last_val = int(last.GetValue(), 16)
        elem_size = first.GetType().GetPointeeType().GetByteSize()

        if elem_size == 0:
            return "size=?"

        size = (last_val - first_val) // elem_size
        return f"size={size}"

    except:
        return "size=?"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        'type summary add -F printers.vector_summary "^std::vector<.+>$"'
    )
    debugger.HandleCommand(
        'type synthetic add -l printers.MSVCVectorProvider "^std::vector<.+>$"'
    )

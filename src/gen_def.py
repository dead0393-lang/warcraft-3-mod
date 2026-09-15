lines = open(r"C:\Games\Warcraft III\acc_src_exports.txt").read().splitlines()
out = ["EXPORTS"]
for line in lines:
    ord_, name = line.split(" ", 1)
    if name[0] in "@?":
        q = '"%s"' % name
        out.append("    %s = mss32_miles.%s" % (q, q))
    else:
        out.append("    %s = mss32_miles.%s" % (name, name))
open(r"C:\Games\Warcraft III\acc_src\mss32.def", "w").write("\n".join(out) + "\n")
print("wrote", len(out) - 1, "exports")

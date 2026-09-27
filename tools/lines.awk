# Count non-blank lines with /* */ and // comments stripped.
# Block comments are the reason this is awk and not wc: a comment can start on
# one line and end on another, so a per-line filter gets it wrong and the number
# is then unreproducible, which is the one thing the README says about it.
function strip(line,   out, i, c, n) {
    n = length(line); out = ""
    i = 1
    while (i <= n) {
        c = substr(line, i, 1)
        if (inblock) {
            if (c == "*" && substr(line, i + 1, 1) == "/") { inblock = 0; i += 2 } else i++
            continue
        }
        if (c == "/" && substr(line, i + 1, 1) == "/") break
        if (c == "/" && substr(line, i + 1, 1) == "*") { inblock = 1; i += 2; continue }
        out = out c; i++
    }
    return out
}
{ line = strip($0); if (line ~ /[^ \t]/) count++ }
END { print count }

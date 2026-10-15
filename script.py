with open("tipe.c", "r") as f:
    lines = f.readlines()

for i in range(0, len(lines), 32):
    print("""\\begin{frame}[fragile]{Annexe}
\\tiny
\\begin{verbatim}
""" + "".join(lines[i:i+32]) + """\\end{verbatim}
\\end{frame}""")

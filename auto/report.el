;; -*- lexical-binding: t; -*-

(TeX-add-style-hook
 "report"
 (lambda ()
   (TeX-add-to-alist 'LaTeX-provided-class-options
                     '(("article" "11pt" "letterpaper")))
   (TeX-add-to-alist 'LaTeX-provided-package-options
                     '(("geometry" "margin=1in") ("amsmath" "") ("amssymb" "") ("graphicx" "") ("booktabs" "") ("listings" "") ("xcolor" "") ("hyperref" "") ("microtype" "") ("parskip" "") ("enumitem" "") ("fancyhdr" "") ("titlesec" "") ("caption" "") ("subcaption" "") ("float" "")))
   (TeX-run-style-hooks
    "latex2e"
    "article"
    "art11"
    "geometry"
    "amsmath"
    "amssymb"
    "graphicx"
    "booktabs"
    "listings"
    "xcolor"
    "hyperref"
    "microtype"
    "parskip"
    "enumitem"
    "fancyhdr"
    "titlesec"
    "caption"
    "subcaption"
    "float")
   (LaTeX-add-labels
    "tab:files"
    "sec:further-work"))
 :latex)


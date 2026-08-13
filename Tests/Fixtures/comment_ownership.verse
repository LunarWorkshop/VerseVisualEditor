CommentOwnership(Condition : logic) : int =
    # prefix first
    First := 1 # postfix first
    if (Condition?):
        # prefix nested
        Second := First <# inline nested #> + 1
        Second

BraceContainer := class {
    # Leading body comment.
    BraceValue : int = 1

    NestedEmpty := class {}
}

ColonContainer := module:
    # Colon body comment.
    NestedStruct := struct:
        NestedValue : int = 2


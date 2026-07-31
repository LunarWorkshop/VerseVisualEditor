NonFailableCall(Input : int)<computes> : int =
    Abs(Input)

FailableCast(Input : float)<decides><reads> : int =
    Int[Input]

FailableCall(Input : float)<decides><reads> : int =
    FailableCast[Input]

FailureOnlySource()<decides><computes> : void =
    false?

FailableVoidCall()<decides><computes> : void =
    FailureOnlySource[]

NonFailableOperator(Input : int)<computes> : int =
    Input + 1

FailableOperator(Input : int)<decides><computes> : void =
    Input < 0

PredicateBinding(MaybeValue : ?int)<allocates><reads> : int =
    if (Value := MaybeValue?; var MutableValue : int = Value):
        Value + MutableValue
    else:
        0

EditableNestedPredicate(Input : int)<computes> : int =
    if (Input > 0; <# comment remains fixed when ownership is ambiguous #> Input < 10):
        if (Input <> 5):
            Input
        else:
            0
    else:
        0

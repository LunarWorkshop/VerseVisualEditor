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

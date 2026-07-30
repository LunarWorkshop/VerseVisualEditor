Transform<public>(Used : int, Unused : string)<computes> : int =
    Used + Used

Describe(Value : float, Label : string)<transacts> : string =
    Label

ChooseLast(First : int, Last : float)<computes> : float =
    First

    Last

AddIntLiteral(Input : int)<computes> : int =
    Input + 1

AddNegativeIntLiteral(Input : int)<computes> : int =
    Input + -12

AddFloat(Left : float, Right : float)<computes> : float =
    Left + Right

AddArray(Left : []int, Right : []int)<computes> : []int =
    Left + Right

AddConflict(Left : int, Right : float)<computes> : int =
    Left + Right

AddChain(A : int, B : int, C : int)<computes> : int =
    A + B + C

Subtract(Left : int, Right : int)<computes> : int =
    Left - Right

Multiply(Left : int, Right : int)<computes> : int =
    Left * Right

Divide(Left : float, Right : float)<computes> : float =
    Left / Right

Equal(Left : int, Right : int)<decides><computes> : void =
    Left = Right

NotEqual(Left : int, Right : int)<decides><computes> : void =
    Left <> Right

LessThan(Left : int, Right : int)<decides><computes> : void =
    Left < Right

LessThanOrEqual(Left : int, Right : int)<decides><computes> : void =
    Left <= Right

GreaterThan(Left : int, Right : int)<decides><computes> : void =
    Left > Right

GreaterThanOrEqual(Left : int, Right : int)<decides><computes> : void =
    Left >= Right

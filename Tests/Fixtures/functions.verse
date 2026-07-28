Transform<public>(Used : int, Unused : string)<computes> : int =
    Used + Used

Describe(Value : float, Label : string)<transacts> : string =
    Label

ChooseLast(First : int, Last : float)<computes> : float =
    First

    Last

AddIntLiteral(Input : int)<computes> : int =
    Input + 1

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

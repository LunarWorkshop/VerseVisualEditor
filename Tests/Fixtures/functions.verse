Transform<public>(Used : int, Unused : string)<computes> : int =
    Used + Used

Describe(Value : float, Label : string)<transacts> : string =
    Label

PassLogic(Input : logic)<computes> : logic = Input
PassInteger(Input : int)<computes> : int = Input
PassFloat(Input : float)<computes> : float = Input
PassString(Input : string)<computes> : string = Input
PassCharacter(Input : char)<computes> : char = Input

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

ComplexBinary(A : int, B : int, C : int, D : int, E : int, F : int)<computes> : int =
    A + (B - C) * ((D - E) / F)

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

NegateValue(Input : int)<computes> : int =
    -Input

CallAbsolute(Input : int)<computes> : int =
    Abs(Input)

CallNamed(Input : int)<computes> : int =
    AcceptNamed(?Value := Input)

EmptyFunction()<computes> : void = {}

ControlIf(Input : int)<computes> : int =
    if (Input > 0):
        Input
    else:
        0

ControlIfMultiple(Input : int)<computes> : int =
    if (Input > 0; Input < 10):
        Input
    else:
        0

ControlIfBraces(Input : int)<computes> : int =
    if (Input > 0) { Input } else { 0 }

ControlIfNested(Input : int)<computes> : int =
    if (Input > 0):
        if (Input < 10):
            Input
        else:
            10
    else:
        0

ControlFor(Values : []int)<computes> : void =
    for (Value : Values):
        Value

ControlForMultiple(Values : []int)<computes> : void =
    for (Value : Values, Value > 0, Other : Values, Other <> Value):
        Value + Other

ControlForBraces(Values : []int)<computes> : void =
    for (Value : Values) { Value }

ControlForMap(Values : [int]int)<computes> : void =
    for (Key -> Value : Values):
        Key + Value

ControlLoop()<computes> : void =
    loop:
        break

ControlSync()<suspends> : tuple(int, int) =
    sync:
        block:
            1
        2

LocalDefinitions(Input : int)<computes> : int =
    var Total : int = Input
    Offset : int = 1
    Total + Offset

OperatorInitializer(Input : int)<computes> : int =
    Difference : int = Input - 1
    Difference

# A deliberately formatted round-trip fixture.
ExampleModule := module:

	ExampleValue : int = 42

	ExampleFunction(Input : int)<transacts> : int =
	{
		Input + ExampleValue
	}

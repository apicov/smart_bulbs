from pytest_embedded_idf.dut import IdfDut


def test_running(dut: IdfDut) -> None:
    """The bulb boots, starts the light engine and reaches the main loop."""
    dut.expect('light_engine: Started')
    dut.expect('main: pico-bulb is running')

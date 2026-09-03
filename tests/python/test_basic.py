import pytest

import lightusd
import typeguard

def test_token():
    a = lightusd.Token("bora")
    del a

def test_token_invalid_numeric():
    # raise error
    with pytest.raises(typeguard.TypeCheckError):
        a = lightusd.Token(1)


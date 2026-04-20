import pytest


@pytest.fixture
def user_credentials():
    return {"email": "test@example.com", "password": "s3cret-pw-12345"}

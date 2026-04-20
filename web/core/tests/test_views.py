import pytest
from django.contrib.auth import get_user_model
from django.urls import reverse

pytestmark = pytest.mark.django_db


@pytest.fixture
def authed_client(client):
    User = get_user_model()
    user = User.objects.create_user(email="alice@example.com", password="pw12345678")
    client.force_login(user)
    return client


def test_home_redirects_anonymous_to_login(client):
    response = client.get(reverse("core:home"))
    assert response.status_code == 302
    assert "/accounts/login/" in response.url


def test_home_renders_for_authed_user(authed_client):
    response = authed_client.get(reverse("core:home"))
    assert response.status_code == 200
    assert b"alice@example.com" in response.content


def test_home_has_logout_link(authed_client):
    response = authed_client.get(reverse("core:home"))
    assert response.status_code == 200
    assert b"Sign out" in response.content or b"Log out" in response.content

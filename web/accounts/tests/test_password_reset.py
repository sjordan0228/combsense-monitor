import pytest
from django.contrib.auth import get_user_model
from django.core import mail
from django.urls import reverse

pytestmark = pytest.mark.django_db


@pytest.fixture
def existing_user():
    User = get_user_model()
    return User.objects.create_user(email="alice@example.com", password="old-pw-12345")


def test_password_reset_form_renders(client):
    response = client.get(reverse("accounts:password_reset"))
    assert response.status_code == 200


def test_password_reset_sends_email(client, existing_user):
    response = client.post(
        reverse("accounts:password_reset"),
        {"email": "alice@example.com"},
    )
    assert response.status_code == 302
    assert len(mail.outbox) == 1
    assert "alice@example.com" in mail.outbox[0].to
    assert "reset" in mail.outbox[0].subject.lower()


def test_password_reset_silent_for_unknown_email(client):
    response = client.post(
        reverse("accounts:password_reset"),
        {"email": "nobody@example.com"},
    )
    # Django silently succeeds to prevent email enumeration
    assert response.status_code == 302
    assert len(mail.outbox) == 0

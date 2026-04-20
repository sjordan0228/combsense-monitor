from django.contrib.auth.forms import AuthenticationForm
from django import forms


class EmailAuthenticationForm(AuthenticationForm):
    """Login form that accepts email in place of username."""

    username = forms.EmailField(
        label="Email",
        widget=forms.EmailInput(attrs={"autofocus": True, "autocomplete": "email"}),
    )

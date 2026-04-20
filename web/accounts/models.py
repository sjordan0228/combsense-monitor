from django.contrib.auth.models import AbstractUser


class User(AbstractUser):
    """Custom user model — fields added in Task 3."""

    class Meta:
        swappable = "AUTH_USER_MODEL"

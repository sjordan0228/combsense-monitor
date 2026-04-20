from django.urls import path

from . import views

app_name = "accounts"

urlpatterns = [
    path("login/", views.CombSenseLoginView.as_view(), name="login"),
    path("logout/", views.CombSenseLogoutView.as_view(), name="logout"),
]

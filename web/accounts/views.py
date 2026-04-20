from django.contrib.auth.views import LoginView, LogoutView

from .forms import EmailAuthenticationForm


class CombSenseLoginView(LoginView):
    authentication_form = EmailAuthenticationForm
    template_name = "registration/login.html"
    redirect_authenticated_user = True


class CombSenseLogoutView(LogoutView):
    http_method_names = ["post", "options"]  # POST only, no GET logout

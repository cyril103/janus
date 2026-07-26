(() => {
  const notice = document.createElement("div");
  notice.className = "copy-confirmation";
  notice.setAttribute("role", "status");
  notice.setAttribute("aria-live", "polite");
  notice.textContent = "Code copié";
  document.body.appendChild(notice);

  let timeout;
  document.addEventListener("click", (event) => {
    const button = event.target.closest(".md-clipboard");
    if (!button) return;
    window.clearTimeout(timeout);
    notice.classList.add("is-visible");
    timeout = window.setTimeout(() => notice.classList.remove("is-visible"), 1400);
  });
})();

declare global {
  interface Document {
    mozCancelFullScreen?: () => Promise<void>;
    msExitFullscreen?: () => Promise<void>;
    webkitExitFullscreen?: () => Promise<void>;
    mozFullScreenElement?: Element;
    msFullscreenElement?: Element;
    webkitFullscreenElement?: Element;
  }

  interface HTMLElement {
    msRequestFullscreen?: () => Promise<void>;
    mozRequestFullscreen?: () => Promise<void>;
    webkitRequestFullscreen?: () => Promise<void>;
  }
}

const createDoubleClickListener = (canvas: HTMLCanvasElement) => {
  window.addEventListener("dblclick", () => {
    const fullScreenElement =
      document.fullscreenElement || document.webkitFullscreenElement;

    if (!fullScreenElement) {
      if (canvas.requestFullscreen) {
        canvas.requestFullscreen();
      } else if (canvas.webkitRequestFullscreen) {
        // Does not work on safari mobile
        canvas.webkitRequestFullscreen();
      }
    } else {
      document.exitFullscreen();
    }
  });
};

export { createDoubleClickListener };

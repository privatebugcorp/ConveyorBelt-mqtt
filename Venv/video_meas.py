import subprocess #SUBB to libcamera vid used here
import cv2 #decoding jpeg and other video relation functions
import numpy as np #for working with arrays 
import imutils # used in grab contours, to organize detected points (contours)
from imutils import perspective
from scipy.spatial.distance import euclidean # calculates distance between captured points (pix4metric)
import time
import threading
from flask import Blueprint, Response

video_bp = Blueprint('video_bp', __name__)

#  glob variables 
latest_frame = None           # Stores most recent JPEG-encoded frame
frame_lock = threading.Lock() # Ensures thread-safe updates to latest_frame

# global callback function pointer that we can set from app.py
measurement_callback = None

def set_measurement_callback(cb):
    """
    Register a callback function that will be called whenever
    a new (width_cm, height_cm) measurement is detected.
    """
    global measurement_callback
    measurement_callback = cb

#  Main capture function 
def capture_measured_video():
    """
    Spawns a 'libcamera-vid' subprocess reading MJPEG frames, processes them
    to find an object's width/height in cm, and calls 'measurement_callback'
    when a new measurement is detected.
    """
    global latest_frame

    # Command to start libcamera-vid in MJPEG mode
    command = [
        "libcamera-vid", "--inline", "--timeout", "0", "--framerate", "30",
        "--width", "640", "--height", "480", "--codec", "mjpeg", "--output", "-"
    ]
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0
    )

    # calibrate 'pixels_per_metric' to suit camera setup
    pixels_per_metric = 75.0  # e.g. 75 px = 1 cm

    # define region of interest (ROI) in the frame
    roi_x, roi_y, roi_w, roi_h = 140, 0, 260, 280

    # background subtractor (optional) for better separate detected obj from bckground
    bg_subtractor = cv2.createBackgroundSubtractorMOG2()

    bytes_buffer = b""
    frame_counter = 0

    while True:
        chunk = process.stdout.read(1024)
        if not chunk:
            # if no data, process ended or crashed
            break

        bytes_buffer += chunk
        start_idx = bytes_buffer.find(b'\xff\xd8')  # JPEG start
        end_idx   = bytes_buffer.find(b'\xff\xd9')  # JPEG end

        # if we don't see a full JPEG yet, keep reading
        if start_idx == -1 or end_idx == -1:
            continue

        # extract one complete JPEG
        jpg = bytes_buffer[start_idx:end_idx+2]
        bytes_buffer = bytes_buffer[end_idx+2:]

        # decode into a BGR frame. decode JPEG data into image (matrix) using OpenCV
        frame = cv2.imdecode(np.frombuffer(jpg, dtype=np.uint8), cv2.IMREAD_COLOR)
        if frame is None:
            continue

        frame_counter += 1
        # example skip: only process every 3rd frame for optimilise 
        if frame_counter % 3 != 0:
            continue

        # draw ROI on the frame (for visual reference)
        cv2.rectangle(frame, (roi_x, roi_y), (roi_x+roi_w, roi_y+roi_h),
                      (0, 0, 255), 2)

        # Crop the ROI
        roi = frame[roi_y:roi_y+roi_h, roi_x:roi_x+roi_w].copy()

        # Optionally background subtraction
        fg_mask = bg_subtractor.apply(roi)

        # Convert to grayscale & mask with foreground
        # Grayscale conversion, masking only the moving region, 
        # Gaussian blurring, and histogram equalization all improve the quality of edge detection.
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = cv2.bitwise_and(gray, gray, mask=fg_mask)
        gray = cv2.GaussianBlur(gray, (5, 5), 0)
        gray = cv2.equalizeHist(gray)  # Enhance contrast

        # adaptive thresholding (Otsu) to determine thresholds for edge detection.
        high_thresh, _ = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        low_thresh = 0.5 * high_thresh
        edged = cv2.Canny(gray, low_thresh, high_thresh)
        edged = cv2.dilate(edged, None, iterations=1)
        edged = cv2.erode(edged, None, iterations=1)

        # Find contours and calculate
        cnts = cv2.findContours(edged.copy(), cv2.RETR_EXTERNAL,
                                cv2.CHAIN_APPROX_SIMPLE)
        cnts = imutils.grab_contours(cnts)

        # Filter contours (by area, aspect ratio, etc.)
        valid_contours = []
        for c in cnts:
            area = cv2.contourArea(c)
            # skip too small or too large
            if area < 500 or area > (roi_w * roi_h * 0.5):
                continue

            rect = cv2.minAreaRect(c)
            (width, height) = rect[1]  # (w, h) in pixels
            if height == 0:
                continue

            aspect_ratio = width / height if width < height else height / width
            # e.g. skip if aspect ratio is extreme
            if aspect_ratio < 0.5:
                continue
            valid_contours.append(c)

        if valid_contours:
            # takes the largest detected contour
            largest_contour = max(valid_contours, key=cv2.contourArea)
            rect = cv2.minAreaRect(largest_contour)
            box = cv2.boxPoints(rect)
            box = perspective.order_points(box)

            # compute real dimensions
            (tl, tr, br, bl) = box
            width_pixels  = euclidean(tl, tr)
            height_pixels = euclidean(tr, br)

            dimWidth  = width_pixels  / pixels_per_metric
            dimHeight = height_pixels / pixels_per_metric

            # if big enough to matter, send it to the callback,
            # items should be bigger for distance sensor
            if dimWidth >= 1.0 and dimHeight >= 1.0:
                # 1) Call the callback
                if measurement_callback is not None:
                    measurement_callback(dimWidth, dimHeight)

                # 2) Draw bounding box for display in API
                box += np.array([roi_x, roi_y])
                cv2.drawContours(frame, [box.astype("int")], -1,
                                 (0, 255, 0), 2)
                midX = int((tl[0] + br[0]) / 2) + roi_x
                midY = int((tl[1] + br[1]) / 2) + roi_y
                cv2.putText(frame,
                            f"{dimWidth:.2f}cm x {dimHeight:.2f}cm",
                            (midX - 50, midY),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                            (0, 255, 0), 2)

        # encode the frame for MJPEG streaming after all modifications (e.g. ROI)
        ret, encoded_frame = cv2.imencode(".jpg", frame)
        if ret:
            with frame_lock:
                latest_frame = encoded_frame.tobytes()

# ------------------ MJPEG Streaming Routes --------------------
def generate_measured_stream():
    """
    A generator function that yields frames in MJPEG format.
    Used by Flask's Response() to stream video in /measured_video route.
    """
    global latest_frame
    while True:
        frame_copy = None
        with frame_lock:
            if latest_frame is not None:
                frame_copy = latest_frame

        if frame_copy is not None:
            yield (b"--frame\r\n"
                   b"Content-Type: image/jpeg\r\n\r\n" +
                   frame_copy +
                   b"\r\n")

        # Limit frame rate
        time.sleep(0.03)

@video_bp.route('/measured_video')
def measured_video():
    """
    Provide the live measured video feed (multipart MJPEG).
    """
    print("[video_meas] /measured_video: starting MJPEG stream")
    return Response(
        generate_measured_stream(),
        mimetype="multipart/x-mixed-replace; boundary=frame"
    )

@video_bp.route('/measured_view') #NOT necessary
def measured_view():
    """
    A simple HTML page that displays the /measured_video feed in an <img>.
    """
    html_content = """
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8">
      <title>Measured Video Stream</title>
    </head>
    <body>
      <h1>Measured Video Stream</h1>
      <p>Refresh this page any time – it won't restart the camera capture!</p>
      <img src="/measured_video" alt="Measured Video" style="width:640px; height:480px;">
    </body>
    </html>
    """
    return html_content

function setup() {
  //Create a canvas of dimensions given by current browser window
  createCanvas(windowWidth, windowHeight);

  //text font
  textFont('Courier New');
}

function draw() {
  clear();

  //Read buffer with index 0 coming from render.cpp.
  let message = Bela.data.buffers[0];
  background(255);


  //Format and display text
  fill(100, 0, 255);
  //Adjust the size of the text to the window width
  textSize(windowHeight / 8);
  //Display text
  if (!message) {
    message = 'nothing received yet';
  }
  text(message, windowHeight / 8, windowHeight / 8);


}

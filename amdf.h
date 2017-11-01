
class Amdf {
public:
  int jumpValue;
  float frequencyEstimate;
  int amdfValue;
  bool amdfIsDone = true;
  float jumpDifference = 0;

  float pitchtrackingAmdfScore;


  Amdf(int longestExpectedPeriodOfSignal, int shortestExpectedPeriodOfSignal){
    correlationWindowSize = longestExpectedPeriodOfSignal * amdf_C;
    // searchWindowSize = longestExpectedPeriodOfSignal - correlationWindowSize;
    searchWindowSize = longestExpectedPeriodOfSignal - shortestExpectedPeriodOfSignal;// - correlationWindowSize;
    nrOfTestedSamplesInCorrelationWindow = correlationWindowSize / jumpLengthBetweenTestedSamples;

    weightIncrement = maxWeight / searchWindowSize;
  }

  void setup(int sampleRate){
    this->sampleRate = sampleRate;
  }
  void initiateAMDF(int searchIndexStart, int compareIndexStart, float * sampleBuffer, int bufferLength);
  bool updateAMDF();

private:
  const float amdf_C = 2.0/8.0;
  const int jumpLengthBetweenTestedSamples = 10;
  const float maxWeight = 0.05f;
  float weight;
  float weightIncrement;
  float filter_C = 0.5;

  float amdfScore; // low value means good correlation. high value means big difference between the compared windows
  float bestSoFar;
  int bestSoFarIndex;
  int bestSoFarIndexJump;

  // float pitchtrackingAmdfScore;
  float pitchtrackingBestSoFar;
  float pitchtrackingBestIndexJump;

  int bufferLength;
  int correlationWindowSize;
  float nrOfTestedSamplesInCorrelationWindow;
  int searchWindowSize;

  int currentSearchIndex;

  int sampleRate;
  float * sampleBuffer;
  int searchIndexStart;
  int searchIndexStop;
  int compareIndexStart;
  int compareIndexStop;
};

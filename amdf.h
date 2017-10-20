
class Amdf {
public:
  float bestSoFar;
  int bestSoFarIndex;
  int bestSoFarIndexJump;

  Amdf(int longestExpectedPeriodOfSignal){
    correlationWindowSize = longestExpectedPeriodOfSignal * amdf_C;
    searchWindowSize = longestExpectedPeriodOfSignal - correlationWindowSize;
  }

  void initiateAMDF(int searchIndexStart, int compareIndexStart, float * sampleBuffer, int bufferLength);
  bool updateAMDF();

private:
  const float amdf_C = 3.0/8.0;
  const int jumpLengthBetweenTestedSamples = 3;
  int correlationWindowSize;
  int searchWindowSize;

  float * sampleBuffer;
  int bufferLength;
  int searchIndexStart;
  int searchIndexStop;
  int currentSearchIndex;
  int compareIndexStart;
  int compareIndexStop;
  bool amdfIsDone = true;
  float magSum = 0;

};

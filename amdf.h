
class Amdf {
public:
  int jumpValue;
  int amdfValue;
  bool amdfIsDone = true;

  Amdf(int longestExpectedPeriodOfSignal){
    correlationWindowSize = longestExpectedPeriodOfSignal * amdf_C;
    searchWindowSize = longestExpectedPeriodOfSignal - correlationWindowSize;
  }

  void initiateAMDF(int searchIndexStart, int compareIndexStart, float * sampleBuffer, int bufferLength);
  bool updateAMDF();

private:
  const float amdf_C = 3.0/8.0;
  const int jumpLengthBetweenTestedSamples = 10;
  int bufferLength;
  int correlationWindowSize;
  int searchWindowSize;

  float magSum = 0;
  int currentSearchIndex;

  float bestSoFar;
  int bestSoFarIndex;
  int bestSoFarIndexJump;

  float * sampleBuffer;
  int searchIndexStart;
  int searchIndexStop;
  int compareIndexStart;
  int compareIndexStop;

};

#include <Geometry.h>
#include <SurfaceQuadrangulation.h>
#include <cmath>
#include <queue>
#include <set>

bool ttk::SurfaceQuadrangulation::hasCommonManifold(
  const std::vector<size_t> &verts) const {

  // get the manifold of every neighbors of our input vector points
  std::vector<std::set<SimplexId>> common_manifolds(verts.size());

  // TTK identifiers of input vertices
  std::vector<SimplexId> vertsId(verts.size());

  std::transform(verts.begin(), verts.end(), vertsId.begin(),
                 [&](size_t a) { return criticalPointsIdentifier_[a]; });

  // iterate around a neighborhood around the critical points to store
  // their manifold index
  for(size_t i = 0; i < verts.size(); ++i) {

    std::set<SimplexId> processedNeighbors;
    std::queue<SimplexId> neighborsToProcess;

    neighborsToProcess.push(vertsId[i]);

    while(!neighborsToProcess.empty()) {
      auto curr = neighborsToProcess.front();
      neighborsToProcess.pop();
      common_manifolds[i].insert(segmentation_[curr]);
      processedNeighbors.insert(curr);
      auto nneigh = triangulation_->getVertexNeighborNumber(vertsId[i]);
      for(SimplexId j = 0; j < nneigh; ++j) {
        SimplexId next;
        triangulation_->getVertexNeighbor(curr, j, next);
        if(processedNeighbors.find(next) == processedNeighbors.end()) {
          neighborsToProcess.push(next);
        }
      }
      if(processedNeighbors.size() > 20) {
        break;
      }
    }
  }

  // intersect every set to get a unique common manifold
  std::set<SimplexId> curr;
  auto last = common_manifolds[0];

  for(size_t i = 1; i < common_manifolds.size(); ++i) {
    std::set_intersection(last.begin(), last.end(), common_manifolds[i].begin(),
                          common_manifolds[i].end(),
                          std::inserter(curr, curr.begin()));
    std::swap(last, curr);
    curr.clear();
  }

  {
    std::stringstream msg;
    msg << "[SurfaceQuadrangulation] Common manifolds between vertices";
    for(auto &id : vertsId) {
      msg << " " << id;
    }
    msg << ":";
    for(auto &elem : last) {
      msg << " " << elem;
    }
    msg << std::endl;
    dMsg(std::cout, msg.str(), advancedInfoMsg);
  }

  return !last.empty();
}

int ttk::SurfaceQuadrangulation::dualQuadrangulate(
  const std::vector<std::pair<ttk::SimplexId, ttk::SimplexId>> &sepEdges)
  const {

  // quadrangles vertices are only extrema

  // maps sources (saddle points) to vector of their destinations (extrema)
  std::map<SimplexId, std::vector<SimplexId>> sourceDests{};

  for(auto &p : sepEdges) {
    SimplexId i;
    for(i = 0; i < criticalPointsNumber_; i++) {
      if(p.first == criticalPointsCellIds_[i]) {
        break;
      }
    }
    SimplexId j;
    for(j = 0; j < criticalPointsNumber_; j++) {
      if(p.second == criticalPointsCellIds_[j]) {
        break;
      }
    }

    auto &v = sourceDests[i];
    v.emplace_back(j);
  }

  for(auto &elt : sourceDests) {
    auto extrema = elt.second;
    if(extrema.size() == 4) {
      auto i = extrema[0];
      auto j = i;
      auto k = i;
      auto l = i;
      // filter extrema by nature (minimum: 0 or maximum: 2)
      if(criticalPointsType_[extrema[1]] == criticalPointsType_[i]) {
        j = extrema[2];
        k = extrema[1];
        l = extrema[3];
      } else if(criticalPointsType_[extrema[2]] == criticalPointsType_[i]) {
        j = extrema[1];
        k = extrema[2];
        l = extrema[3];
      } else if(criticalPointsType_[extrema[3]] == criticalPointsType_[i]) {
        j = extrema[2];
        k = extrema[3];
        l = extrema[1];
      }
      outputCells_->emplace_back(4);
      outputCells_->emplace_back(i);
      outputCells_->emplace_back(j);
      outputCells_->emplace_back(k);
      outputCells_->emplace_back(l);
    }
  }

  return 0;
}

int ttk::SurfaceQuadrangulation::quadrangulate(
  const std::vector<std::pair<ttk::SimplexId, ttk::SimplexId>> &sepEdges,
  size_t &ndegen) const {
  // quadrangle vertices are either extrema or saddle points

  // separatrices: destinations (extrema) -> sources (saddle points)
  std::vector<std::set<SimplexId>> sepMappingDests(sepEdges.size());

  // number of separatrices coming out of this edge
  std::vector<size_t> pointSepNumber(criticalPointsNumber_);

  for(auto &p : sepEdges) {
    for(SimplexId i = 0; i < criticalPointsNumber_; i++) {
      if(p.first == criticalPointsCellIds_[i]) {
        for(SimplexId j = 0; j < criticalPointsNumber_; j++) {
          if(p.second == criticalPointsCellIds_[j]) {
            sepMappingDests[j].insert(i);
            pointSepNumber[j]++;
            // should we also use the saddle points valence?
            pointSepNumber[i]++;
          }
        }
      }
    }
  }

  // iterate twice over dests
  for(size_t i = 0; i < sepMappingDests.size(); i++) {
    // skip if no sources
    if(sepMappingDests[i].empty()) {
      continue;
    }
    // begin second loop at i+1 to avoid duplicates and improve
    // performance
    for(size_t k = i + 1; k < sepMappingDests.size(); k++) {
      // skip same dest or if no sources
      if(k == i || sepMappingDests[k].empty()) {
        continue;
      }

      // skip if same type (we are looking for quadrangles containing
      // one minimum, one maximum and two saddle points)
      if(criticalPointsType_[k] == criticalPointsType_[i]) {
        continue;
      }

      // list of common sources to i and k
      std::vector<SimplexId> common_dests;
      std::set_intersection(
        sepMappingDests[i].begin(), sepMappingDests[i].end(),
        sepMappingDests[k].begin(), sepMappingDests[k].end(),
        std::back_inserter(common_dests));

      // find at least two common sources: j and l
      if(common_dests.size() >= 2) {

        // iterate over all possible common sources
        for(size_t m = 0; m < common_dests.size(); m++) {
          // avoid duplicates by beginning at m+1
          for(size_t n = m + 1; n < common_dests.size(); n++) {

            // gotcha!
            size_t j = common_dests[m];
            size_t l = common_dests[n];

            // check for a common shared manifold (looking around
            // saddle points only seems to be sufficient)
            std::vector<size_t> verts{j, l};

            bool validQuad = hasCommonManifold(verts);

            // fill output vector
            if(validQuad) {
              outputCells_->emplace_back(4);
              outputCells_->emplace_back(i);
              outputCells_->emplace_back(j);
              outputCells_->emplace_back(k);
              outputCells_->emplace_back(l);
            }
          }
        }
      } else if(common_dests.size() == 1
                && (sepMappingDests[i].size() == 1
                    || sepMappingDests[k].size() == 1)) {
        // we have degenerate quadrangles: i, j, k, j
        size_t j = common_dests[0];
        ndegen++;

        // fill output vector
        outputCells_->emplace_back(4);
        outputCells_->emplace_back(i);
        outputCells_->emplace_back(j);
        outputCells_->emplace_back(k);
        outputCells_->emplace_back(j);
      }
    }
  }

  // post-processing: try to detect missing or extra quadrangles by
  // comparing separatrices number coming out of extrema

  // number of quads that have the critical point as a vertex
  std::vector<size_t> pointQuadNumber(criticalPointsNumber_);

  for(size_t i = 0; i < outputCells_->size() / 5; ++i) {
    pointQuadNumber[outputCells_->at(5 * i + 1)]++;
    pointQuadNumber[outputCells_->at(5 * i + 2)]++;
    pointQuadNumber[outputCells_->at(5 * i + 3)]++;
    pointQuadNumber[outputCells_->at(5 * i + 4)]++;
  }

  // critical points with valence less than the number of quads around
  std::vector<SimplexId> badPoints{};

  for(SimplexId i = 0; i < criticalPointsNumber_; ++i) {
    // should we only detect missing quadrangles, or extra quadrangles too?
    if(pointQuadNumber[i] < pointSepNumber[i]) {
      badPoints.emplace_back(static_cast<size_t>(i));
    }
  }

  // quads that have several (at least two?) bad vertices
  std::vector<size_t> badQuads{};

  for(size_t i = 0; i < outputCells_->size() / 5; ++i) {
    // number of bad vertices in quad
    size_t nbadpoints = 0;
    for(size_t j = 5 * i + 1; j < 5 * (i + 1); ++j) {
      if(std::find(badPoints.begin(), badPoints.end(), outputCells_->at(j))
         != badPoints.end()) {
        nbadpoints++;
      }
    }
    if(nbadpoints >= 2) {
      badQuads.emplace_back(i);
    }
  }

  std::vector<std::pair<SimplexId, size_t>> sepFlatEdgesPos{};

  for(SimplexId i = 0; i < separatriceNumber_; ++i) {
    if(sepMask_[i] == 1) {
      continue;
    }
    sepFlatEdgesPos.emplace_back(std::make_pair(sepCellIds_[i], i));
  }

  std::map<std::pair<size_t, size_t>, size_t> sepMiddles{};

  // subdivise bad quads alongside separatrices
  for(auto &q : badQuads) {
    auto i = outputCells_->at(5 * q + 1);
    auto j = outputCells_->at(5 * q + 2);
    auto k = outputCells_->at(5 * q + 3);
    auto l = outputCells_->at(5 * q + 4);
    findSeparatrixMiddle(j, i, sepFlatEdgesPos, sepMiddles);
    findSeparatrixMiddle(j, k, sepFlatEdgesPos, sepMiddles);
    findSeparatrixMiddle(l, i, sepFlatEdgesPos, sepMiddles);
    findSeparatrixMiddle(l, k, sepFlatEdgesPos, sepMiddles);
  }

  return 0;
}

size_t ttk::SurfaceQuadrangulation::findSeparatrixMiddle(
  const size_t src,
  const size_t dst,
  const std::vector<std::pair<SimplexId, size_t>> &sepFlatEdgesPos,
  std::map<std::pair<size_t, size_t>, size_t> &sepMiddles) const {

  // indices in separatrices point data
  std::vector<std::pair<size_t, size_t>> sepBounds{};

  for(size_t i = 0; i < sepFlatEdgesPos.size(); ++i) {
    auto p = sepFlatEdgesPos[i];
    auto q = sepFlatEdgesPos[i + 1];
    if(p.first == criticalPointsCellIds_[src]
       && q.first == criticalPointsCellIds_[dst]) {
      sepBounds.emplace_back(std::make_pair(p.second, q.second));
    }
  }

  const int dim = 3;

  for(auto &p : sepBounds) {
    auto a = p.first;
    auto b = p.second;

    // check if middle already computed
    auto cached = sepMiddles.find(std::make_pair(a, b));
    if(cached != sepMiddles.end()) {
      continue;
    }

    std::vector<float> distFromA(b - a + 1);
    std::array<float, dim> prev{}, curr{};

    curr[0] = sepPoints_[dim * a];
    curr[1] = sepPoints_[dim * a + 1];
    curr[2] = sepPoints_[dim * a + 2];

    // integrate distances at every point of this separatrix
    for(size_t i = 1; i < b - a + 1; ++i) {
      prev = std::move(curr);
      curr[0] = sepPoints_[dim * (a + i)];
      curr[1] = sepPoints_[dim * (a + i) + 1];
      curr[2] = sepPoints_[dim * (a + i) + 2];
      distFromA[i]
        = distFromA[i - 1] + ttk::Geometry::distance(&curr[0], &prev[0]);
    }

    auto distAB = distFromA.back();
    for(auto &el : distFromA) {
      el = std::abs(el - distAB / 2.0);
    }

    // index in separatrices point data array of separatrix middle
    auto id = a + std::min_element(distFromA.begin(), distFromA.end())
              - distFromA.begin();

    // new point!
    outputPoints_->emplace_back(sepPoints_[dim * id]);
    outputPoints_->emplace_back(sepPoints_[dim * id + 1]);
    outputPoints_->emplace_back(sepPoints_[dim * id + 2]);

    // put separatrix bounds and middle id in cache
    sepMiddles.insert(std::make_pair(std::make_pair(a, b), id));
  }

  return 0;
}

// main routine
int ttk::SurfaceQuadrangulation::execute() const {

  using std::cout;
  using std::endl;
  using std::vector;

  Timer t;

  // clear output
  outputCells_->clear();
  outputPoints_->clear();

  // filter sepCellIds_ array according to sepMask_
  std::vector<SimplexId> sepFlatEdges{};

  for(SimplexId i = 0; i < separatriceNumber_; ++i) {
    if(sepMask_[i] == 1) {
      continue;
    }
    sepFlatEdges.emplace_back(sepCellIds_[i]);
  }

  if(sepFlatEdges.size() % 2 != 0) {
    std::stringstream msg;
    msg << "[SurfaceQuadrangulation] Error: odd number of separatrices edges"
        << endl;
    dMsg(cout, msg.str(), infoMsg);
    return -1;
  }

  // holds separatrices edges for every separatrix
  std::vector<std::pair<SimplexId, SimplexId>> sepEdges{};

  for(size_t i = 0; i < sepFlatEdges.size() / 2; ++i) {
    sepEdges.emplace_back(
      std::make_pair(sepFlatEdges[2 * i], sepFlatEdges[2 * i + 1]));
  }

  // number of degenerate quadrangles
  size_t ndegen = 0;

  if(dualQuadrangulation_) {
    dualQuadrangulate(sepEdges);
  } else {
    // direct quadrangulation with saddle points
    quadrangulate(sepEdges, ndegen);
  }

  // maximum manifold id
  int maxSegId = 0;
  // compute max + 1 of segmentation indices
  for(size_t a = 0; a < segmentationNumber_; ++a) {
    if(segmentation_[a] > maxSegId) {
      maxSegId = segmentation_[a];
    }
  }
  // total number of manifolds
  int nseg = maxSegId + 1;

  // number of produced quads
  size_t quadNumber = outputCells_->size() / 5;

  // print number of quadrangles wrt number of MSC segmentation
  {
    std::stringstream msg;
    msg << "[SurfaceQuadrangulation] " << quadNumber << " quads (" << ndegen
        << " degenerated, " << nseg << " manifolds)" << endl;
    dMsg(cout, msg.str(), detailedInfoMsg);
  }

  {
    std::stringstream msg;
    msg << "[SurfaceQuadrangulation] Produced " << quadNumber
        << " quadrangles after " << t.getElapsedTime() << " s." << endl;
    dMsg(cout, msg.str(), infoMsg);
  }

  return 0;
}

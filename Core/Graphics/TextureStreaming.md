# Texture Streaming TODO

per-mip 청킹에서 출발해 거리 기반 mipmap 스트리밍까지 가는 계획. 단계마다
독립적으로 빌드·검증이 가능하도록 나눴고, 앞 단계가 뒤 단계의 기반이다.
터레인 스트리머(요청 링, 히스테리시스, 예산, 착지 후 승격)와 같은 모델을
텍스처에 적용하는 작업이다.

## 배경

- 텍스처 업로드는 파일 전체(전 mip)가 하나의 job, 하나의 staging span이다.
- 프레임 예산 16 MB, staging ring 32 MB보다 큰 리소스를 위해 예외가 두 개 살아 있다.
  - `TransferContext::TryAdmit`: 빈 프레임이면 예산 초과를 허용한다.
  - `StagingRing::Acquire` 실패 시 `TextureUploadJob` / `GeometryUploadJob`이
    일회성 `DEDICATED_HOST` staging으로 우회한다.
- KTX2는 BasisU 초압축이라 로드 시 전체를 한 번에 BC7로 트랜스코딩한다.
  청킹은 디스크 읽기가 아니라 staging 단계에서 한다.
- 텍스처 residency는 전체 아니면 없음(`ResourceState::Resident`)이고, 그 전에는
  default 텍스처로 그린다.

## 0. 전제 조건

- [ ] 큰 텍스처(env HDR 포함)가 모두 `BakeTextures.ps1`을 거쳐 mip이 베이크된
      KTX2로 오는지 확인한다. PNG/JPG는 `GenerateMipmaps`, HDR은 mip 없음이라
      청킹 대상이 아니다.
- [ ] `Image::QueryStagingBytes`를 확장해 헤더만 열어 **mip별 바이트**
      (`ktxTexture_GetImageSize(level)` × layer)를 돌려주게 한다.
      glTF 파싱 job의 `imageBytes`가 이 목록을 들고 온다.

## 1. per-mip 청킹

목표: 모든 업로드 요청이 예산 이하 크기가 되게 해서 위 예외 두 개를 assert로 바꾼다.
1차에서는 마지막 청크가 착지해야 Resident가 되므로 화면상 변화는 없다.

- [ ] `Image::Load`를 분리한다: 헤더로 VkImage 생성(`CreateFromKtxHeader`) /
      데이터 디코드(blob + mip·layer별 `VkBufferImageCopy`). 디코드 결과는
      `DecodedTexture`로 묶고 청크 job들이 `shared_ptr`로 공유한다.
- [ ] 청크 계획: mip별 바이트를 coarse → fine 순으로 묶어 각 청크가
      예산과 ring 용량의 절반 이하가 되게 한다. 예: job 0 = mip 2~N,
      job 1 = mip 1, job 2 = mip 0. mip 0 하나가 예산을 넘으면 1차에서는 assert
      (sub-mip 행 슬랩 청킹은 선택 항목, 아래 참고).
- [ ] `TextureUploadRequest`에 mip 범위, 청크 바이트, 공유 `DecodedTexture`를
      추가한다. `GLTFLoader::FinalizeLoad` / `ResourceManager::LoadTexture`에서
      요청 하나를 청크 요청 여러 개로 펼쳐 `AssetStreamer::Push`한다.
- [ ] `TextureUploadQueue::Push`의 dedup 키를 텍스처 → 텍스처 + mip 범위로 바꾼다.
- [ ] `AssetStreamer::SubmitQueuedTextures`: 청크 단위로 `TryAdmit`. 같은
      텍스처의 두 번째 청크부터는 앞 청크 job이 **기록 완료**(JobStatus
      COMPLETE)되어 blob이 채워진 뒤에만 admit한다. GPU 착지를 기다리지 않는다.
- [ ] `TextureUploadJob`: blob이 비어 있으면 디코드해 채우고, 자기 mip 범위의
      region만 ring에 memcpy + 복사한다. 마지막 청크 job과 함께 blob을 해제한다.
- [ ] `BarrierBatch::Image`에 `VkImageSubresourceRange` 오버로드를 추가한다.
      청크는 자기 mip 범위만 UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY로
      전환한다 (다른 청크가 이미 바꾼 mip을 건드리면 안 된다).
- [ ] 마지막 청크의 `onLanded`에서 `SetResident`. 청크 착지 수를 세거나
      "마지막 청크" 플래그로 판단한다.
- [ ] 예외 제거: `TryAdmit`의 오버사이즈 분기 → assert,
      `TextureUploadJob`의 `DEDICATED_HOST` fallback 삭제.
- [ ] 자매 작업: `GeometryUploadJob`도 바이트 범위로 쪼갠다(버퍼 복사는 임의
      오프셋에서 잘림). 이것까지 끝나야 `StagingRing::Acquire` 실패를 assert로
      바꿀 수 있다.
- [ ] 검증: 로드 스파이크 동안 ring fallback 카운트 0, 프레임당 admit 바이트가
      예산을 넘는 프레임 없음, 일반 + sync validation 스모크 0 에러.

선택: mip 0이 예산을 넘는 경우(8K BC7 등) `imageOffset.y` / `bufferImageHeight`로
블록 행 단위 슬랩 청킹. 필요한 에셋이 생기면 한다.

## 2. 부분 residency (거친 mip부터 표시)

목표: 전부 착지하기 전에도 올라온 mip만으로 그린다.

- [ ] 텍스처에 `residentFloor`(착지한 가장 정밀한 mip)를 둔다. 청크 착지마다
      내려간다. 상태는 Unloaded / Partial / Resident.
- [ ] floor mip을 `baseMipLevel`로 하는 뷰로 bindless 디스크립터를 갱신한다.
      `Image::GetOrCreateImageView(mip)`가 이미 있다. 뷰의 base mip을 올리면
      하드웨어가 그 mip을 level 0으로 보고 LOD를 계산하므로 셰이더 수정 없음.
- [ ] 뷰·디스크립터 교체는 in-flight 프레임이 이전 뷰를 쓸 수 있으므로 터레인
      슬롯 retire처럼 `MAX_FRAMES_IN_FLIGHT` 프레임 스탬프로 미룬다
      (deferred destruction retire queue와 맞물림).
- [ ] 머티리얼/배치의 "Resident일 때만 바인드" 조건을 "floor가 존재하면 바인드"로
      완화한다. 첫 청크(가장 거친 mip 묶음)가 착지하는 순간부터 보인다.
- [ ] 검증: 부분 착지 중 프레임을 캡처해 안 올라온 mip이 샘플링되지 않는지,
      흐림 → 선명 전환이 깜빡임 없이 되는지 확인.

## 3. wanted mip 계산 (거리 기반)

목표: 어떤 텍스처를 어느 mip까지 올릴지 매 프레임 정한다.

- [ ] 서브메시별 텍셀 밀도를 glTF 로드 시 한 번 계산한다
      (UV 공간 대비 월드 크기, 텍스처 해상도 → 월드 단위당 텍셀 수).
- [ ] 인스턴스별로 카메라 거리와 AABB로 화면 텍셀/픽셀 비율을 구하고
      `wanted = clamp(log2(texelsPerPixel), 0, N-1)`. 머티리얼 → 텍스처로
      그 텍스처를 쓰는 모든 인스턴스의 최소값을 모은다. CPU, RendererBatch의
      인스턴스 테이블 기준.
- [ ] 우선순위 = `residentFloor - wanted`(부족한 mip 수)에 거리를 보조 키로.
      `AssetStreamer`의 FIFO를 우선순위 큐로 바꾸고, 청크 요청은 wanted까지만
      올린다. 카메라가 가까워지면 추가 청크를 요청한다.
- [ ] 히스테리시스: wanted가 floor보다 일정 단계 이상 커진 상태가 N프레임
      이어질 때만 언로드 후보로 올린다.
- [ ] ImGui: 텍스처별 wanted / resident floor / 대기 청크, 전체 예산 사용량.
- [ ] 나중 옵션: GPU 피드백(샘플된 mip을 피드백 버퍼에 기록)으로 근사를 대체.

## 4. 언로드와 메모리 풀

목표: VRAM 예산을 두고 fine mip을 내린다. 설계 선택이 갈리는 단계.

- [ ] 1차: 이미지는 전체 mip 체인으로 할당한 채 유지. "언로드"는 floor를
      올리고 뷰를 바꾸는 것뿐이라 VRAM은 줄지 않고 대역폭/로드 시간만 절약된다.
      단순하고 2·3단계만으로 동작한다.
- [ ] 2차 (VRAM 압박이 실제로 생기는 씬이 있을 때): Vulkan sparse residency로
      mip별 메모리를 바인드/언바인드한다. 작은 mip은 mip tail에 묶여 항상
      상주. `MemoryAllocator`에 sparse 바인드 경로 추가, 디바이스 feature 확인.
      대안은 UE식 "작은 이미지 재생성 + resident mip 복사".
- [ ] 스트리밍 풀 예산(VRAM 비율)과 초과 시 우선순위 낮은 텍스처부터 내리는
      eviction 루프. 터레인 `evictHysteresis`와 같은 규칙.

## 의존 관계

0 → 1 → 2 → 3 → 4. 1만으로 스트리밍 예외 부채가 사라지고, 3까지 하면
로드 스파이크와 초기 화면이 크게 좋아진다. 4의 sparse 바인딩은 필요가
생길 때 붙인다.

## 관련 파일

- `Core/Graphics/TextureUpload.h/.cpp` — 요청, 큐, 업로드 job
- `Core/Graphics/AssetStreamer.cpp` — admission 드레인
- `Core/Graphics/TransferContext.h/.cpp` — `TryAdmit`, `PendingUpload::onLanded`
- `Core/Graphics/StagingRing.h/.cpp` — ring, fallback 카운트
- `Core/Graphics/Vulkans/Image.h/.cpp` — `Load`, `LoadKtxImage`, `QueryStagingBytes`, mip 뷰
- `Core/Graphics/Vulkans/CommandBuffer` BarrierBatch — 서브리소스 범위 배리어
- `Core/Graphics/Vulkans/BindlessTextureManager` — 디스크립터 갱신
- `Core/Utils/GLTFLoader.cpp` — 파싱 job의 `imageBytes`, `FinalizeLoad`
- `Core/Graphics/ResourceManager.cpp` — `LoadTexture`의 요청 생성
- `Core/Graphics/Terrain/TerrainStreamer` — 참고 모델

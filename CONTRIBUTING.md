# Contributing Guidelines

## 프로젝트 개요
**Anggeum-Engine**은 C++17과 Vulkan API를 사용하여 개발된 자체 제작 렌더링 엔진입니다.

## 코딩 규칙

### Include 규칙
- `Device.h`는 `stdafx.h`에 포함되어 있으므로 각 파일에서 별도로 include하지 않습니다.
- 모든 `.cpp` 파일의 include는 항상 `"stdafx.h"`로 시작합니다.
- C++ 표준 라이브러리(std library)는 `stdafx.h`에 정의되어 있으므로 개별 파일에서 include하지 않습니다.
- 
```cpp
// ? 올바른 예시
#include "stdafx.h"
#include "DepthPrePass.h"
// ... 기타 includes

// ? 잘못된 예시
#include "DepthPrePass.h"
#include "stdafx.h"  // stdafx.h가 첫 번째가 아님
```
### 주석 작성
- 주석은 항상 영어로 작성한다.

### 코드 언어 및 표준
- C++17 표준 사용
- `using namespace Core;` 패턴 사용

## 코드 수정 원칙

### 1. 코드 위치 변경 금지 (Code Relocation Prohibition)
**명시적으로 요청하지 않는 한 코드를 다른 클래스나 위치로 절대 옮기지 않습니다.**

### 2. glsl 파일 생성 위치
- ./Assets/Shaders/

#### ? 금지 사항
```cpp
// RendererBatch::Draw() 내부의 코드를
// GeometryPass, ShadowPass 등 다른 Pass 클래스로 임의 이동
```

```h
// #include "stdafx.h" 선언 금지
// using namespace Core; 선언 금지
```

#### ? 허용 사항
```cpp
// "이 코드를 GeometryPass로 옮겨줘" 같은
// 명시적 요청이 있을 때만 코드 이동 가능
```

#### 절대 이동 금지 코드 (Critical Code - DO NOT MOVE)
다음 코드는 `RendererBatch::Draw()` 메서드 내부에 유지되어야 하며, **절대 다른 클래스로 이동하지 않습니다**:

```cpp
// Core/Graphics/RendererBatch.cpp - RendererBatches::Draw() 내부
// ?? 절대 이동 금지 - DO NOT MOVE TO OTHER CLASSES
sharedMaterial->SetStorageBuffer(renderFrame, 1, 1, _transformBatch.TransformBuffer);
sharedMaterial->SetStorageBuffer(renderFrame, 1, 2, _instanceBuffer);
```

**이유**: 이 코드를 다른 위치로 옮기면:
- 디버깅 시간이 증가합니다
- 예상치 못한 버그가 발생합니다
- 원복 및 재수정에 시간이 소모됩니다

### 2. 기존 구조 유지 (Preserve Existing Structure)
**기존 코드의 위치와 구조를 최대한 유지합니다.**

- ? 함수의 위치를 유지
- ? 클래스 멤버 변수 순서를 유지
- ? 파일 구조를 유지
- ? 불필요한 리팩토링 금지
- ? 요청되지 않은 최적화 금지
- ? 관련 없는 코드 수정 금지

### 3. 변경 사항 명확히 표시 (Clear Change Documentation)
**수정 시 어떤 부분을 수정했는지만 명확히 보여줍니다.**

```cpp
// ? 좋은 예시 - 변경 부분만 표시
void SomeFunction()
{
    // ... existing code ...
    
    // Added: Update descriptor sets
    material->UpdateDescriptorSets(descriptorSets);
    
    // ... existing code ...
}

// ? 나쁜 예시 - 전체 파일 내용 표시
// (변경되지 않은 코드까지 모두 보여주지 않음)
```

### 4. 최소 변경 원칙 (Minimal Changes)
**요청된 기능을 구현하는데 필요한 최소한의 변경만 수행합니다.**

- 요청된 기능에 직접 관련된 코드만 수정
- 부수적인 "개선" 지양
- "Keep the existing logic and structure unless explicitly asked to change"

## 코드 리뷰 체크리스트

코드를 수정하기 전에 다음을 확인하세요:

- [ ] 명시적으로 요청된 변경만 수행했는가?
- [ ] 코드를 다른 클래스나 파일로 옮기지 않았는가?
- [ ] 기존 코드 구조를 유지했는가?
- [ ] `stdafx.h`가 첫 번째 include인가?
- [ ] 변경 부분만 명확히 표시했는가?
- [ ] 불필요한 리팩토링을 하지 않았는가?
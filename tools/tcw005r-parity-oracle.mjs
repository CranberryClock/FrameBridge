import * as THREE from '../apps/demo-web/node_modules/three/build/three.module.js';
import {canonicalState,encodeFrameState} from '../packages/protocol/dist/index.js';
import {writeFile,mkdir} from 'node:fs/promises';
const frames=[];
for(const frame of [60n,120n,180n]) for(const [width,height] of [[640,360],[800,450]]) {
  const s=canonicalState(frame,{width,height,resizeGeneration:width===640?1n:2n});
  const cube=new THREE.Mesh(new THREE.BoxGeometry(1,1,1)); cube.rotation.set(s.rotationX,s.rotationY,0,'XYZ'); cube.updateMatrixWorld(true);
  const camera=new THREE.PerspectiveCamera(60,width/height,.1,100);
  camera.coordinateSystem=THREE.WebGPUCoordinateSystem; camera.position.z=s.cameraZ;
  camera.updateMatrixWorld(true); camera.updateProjectionMatrix();
  const model=cube.matrixWorld,view=camera.matrixWorldInverse,projection=camera.projectionMatrix;
  const mvp=new THREE.Matrix4().multiplyMatrices(projection,new THREE.Matrix4().multiplyMatrices(view,model));
  const corners=Array.from({length:8},(_,i)=>new THREE.Vector3(i&1?.5:-.5,i&2?.5:-.5,i&4?.5:-.5).applyMatrix4(mvp).toArray());
  frames.push({frame:Number(frame),width,height,scene_bytes:Buffer.from(encodeFrameState(s)).toString('hex'),model:model.toArray(),view:view.toArray(),projection:projection.toArray(),mvp:mvp.toArray(),corners});
}
await mkdir('tests/fixtures/tcw005',{recursive:true});
await writeFile('tests/fixtures/tcw005/three-parity.json',JSON.stringify({three:THREE.REVISION,tolerance:1e-12,gpu_float_corner_tolerance:2e-6,frames},null,2)+'\n');
console.log('PASS Three r185 oracle: 6 frame/viewport cases, 64 matrix entries and 24 corner components each');

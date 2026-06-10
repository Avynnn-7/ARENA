import 'katex/dist/katex.min.css';
import formulas from './prerendered-formulas.json';

type Key = keyof typeof formulas;

export function Tex({ name, className }: { name: Key; className?: string }) {
  return <span className={className} dangerouslySetInnerHTML={{ __html: formulas[name] }} />;
}

export function TexBlock({ name, className = '' }: { name: Key; className?: string }) {
  return <div className={className} dangerouslySetInnerHTML={{ __html: formulas[name] }} />;
}
